#!/usr/bin/env python3
"""Runs the Godot AI eval set and reports how many tasks the assistant gets right.

Each task is a prompt plus checks on what the editor looks like afterwards. A
task runs in its own headless editor and its own scratch project, driven by
runner.gd, so tasks cannot affect each other and a crash only loses one task.

Two ways to answer the prompts:

  --mock      Replays the reference answers stored with the tasks through a
              local OpenAI-compatible server. No network, no API key, fully
              deterministic: this checks the harness and the agent loop itself,
              and that every task's checks are satisfiable.

  (default)   Asks a real model. This is the score that matters when a prompt,
              a gotcha or the retry logic changes: run it before and after and
              compare with --baseline.

Examples:

  python run_evals.py --godot ../../../bin/godot.windows.editor.x86_64.console.exe --mock
  python run_evals.py --godot <editor> --provider deepseek --out results/deepseek.json
  python run_evals.py --godot <editor> --provider deepseek --knowledge-dir ../knowledge \\
      --baseline results/deepseek.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

EVALS_DIR = Path(__file__).resolve().parent
MODULE_DIR = EVALS_DIR.parent
TASKS_DIR = EVALS_DIR / "tasks"
PROJECT_TEMPLATE = EVALS_DIR / "project_template"
RUNNER_SCRIPT = EVALS_DIR / "runner.gd"

MOCK_PROVIDER = "openai"
# Sent instead of a reply that requires an image when the request has none;
# it contains no code, so the task's checks fail.
MOCK_NO_IMAGE_REPLY = "The request did not carry the screenshot."
MOCK_MODEL = "mock-model"
MOCK_API_KEY = "mock-key"
MOCK_DELTA_CHARS = 24  # Characters of reply text per streamed delta event.
MOCK_CHUNK_BYTES = 7  # Small and odd, so multi-byte characters get split across chunks.
STARTUP_ALLOWANCE_SEC = 120.0  # Editor start-up and project import, on top of the task budget.
LOG_TAIL_LINES = 15

logger = logging.getLogger("evals")


# A canned reply: the text to stream, or a dict for the special cases —
# {"status": 400, "body": "..."} answers with an HTTP error, and
# {"requires_image": true, "text": "..."} streams the text only if the
# request carries an image, and {"finish": "length", "text": "..."} ends the
# stream the way a reply cut off at the output limit ends.
MockReply = str | dict[str, Any]


@dataclass(frozen=True)
class Task:
    id: str
    data: dict[str, Any]

    @property
    def category(self) -> str:
        return self.id.split(".", 1)[0]

    @property
    def mock_replies(self) -> list[MockReply]:
        return list(self.data.get("mock_replies", []))

    @property
    def mock_only(self) -> bool:
        return bool(self.data.get("mock_only", False))


@dataclass
class TaskResult:
    id: str
    category: str
    passed: bool
    status: str  # "passed", "failed", or why there is no verdict ("timeout", "crashed", ...)
    requests: int = 0
    retries: int = 0
    duration_sec: float = 0.0
    failed_checks: list[str] = field(default_factory=list)
    log_tail: str = ""
    # With --repeat: how many runs gave a verdict, and how many of those passed.
    runs: int = 1
    passes: int = 0
    first_try_passes: int = 0
    # Runs without a verdict (editor crashed, timed out, unreadable result).
    # They say nothing about the model and are kept out of the rates.
    infra_failures: int = 0


@dataclass(frozen=True)
class RunConfig:
    godot: Path
    mock: bool
    provider: str
    model: str
    endpoint: str
    knowledge_dir: str
    timeout_sec: float
    use_editor_config: bool
    keep_projects: bool
    work_dir: Path


def _join_lines(value: Any) -> Any:
    """Task files may write multi-line text as a list of lines, which stays readable in JSON."""
    return "\n".join(value) if isinstance(value, list) else value


def _normalize_reply(reply: Any) -> MockReply:
    if isinstance(reply, dict):
        return {**reply, "text": _join_lines(reply.get("text", ""))}
    return _join_lines(reply)


def _normalize_task(entry: dict[str, Any]) -> dict[str, Any]:
    data = dict(entry)
    data["prompt"] = _join_lines(entry.get("prompt", ""))
    data["mock_replies"] = [_normalize_reply(reply) for reply in entry.get("mock_replies", [])]
    setup = dict(entry.get("setup", {}))
    setup["files"] = {path: _join_lines(content) for path, content in setup.get("files", {}).items()}
    data["setup"] = setup
    return data


def load_tasks(name_filter: str, include_mock_only: bool) -> list[Task]:
    tasks: list[Task] = []
    seen: set[str] = set()
    for path in sorted(TASKS_DIR.glob("*.json")):
        for entry in json.loads(path.read_text(encoding="utf-8")):
            task = Task(id=entry["id"], data=_normalize_task(entry))
            if task.id in seen:
                raise ValueError(f"Duplicate task id {task.id!r} in {path.name}")
            seen.add(task.id)
            if task.mock_only and not include_mock_only:
                continue
            if name_filter and name_filter not in task.id:
                continue
            tasks.append(task)
    return tasks


# --- Mock model server -------------------------------------------------------


class MockModelServer:
    """An OpenAI-compatible streaming endpoint that replays canned replies.

    Each task gets its own base URL (/t/<task id>/v1), so tasks can run in
    parallel and each sees its replies in order: the first request gets the
    first reply, a retry gets the second, and so on. Once a task's replies are
    used up the last one is repeated.
    """

    def __init__(self, tasks: list[Task]) -> None:
        self._replies = {task.id: task.mock_replies for task in tasks}
        self._served: dict[str, int] = {}
        self._lock = threading.Lock()
        self._server = ThreadingHTTPServer(("127.0.0.1", 0), self._make_handler())
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()

    def endpoint_for(self, task: Task, attempt: int = 0) -> str:
        host, port = self._server.server_address[:2]
        # Each run of a task replays the replies from the start.
        return f"http://{host}:{port}/t/{task.id}@{attempt}/v1"

    def _next_reply(self, task_id: str) -> MockReply | None:
        with self._lock:
            replies = self._replies.get(task_id.split("@")[0])
            if not replies:
                return None
            index = self._served.get(task_id, 0)
            self._served[task_id] = index + 1
            return replies[min(index, len(replies) - 1)]

    def _make_handler(self) -> type[BaseHTTPRequestHandler]:
        server = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self) -> None:  # noqa: N802 (name fixed by http.server)
                length = int(self.headers.get("Content-Length", "0"))
                request_body = self.rfile.read(length)
                parts = self.path.strip("/").split("/")
                reply = server._next_reply(parts[1]) if len(parts) >= 2 and parts[0] == "t" else None
                if reply is None:
                    self.send_error(404, "No mock reply for this task")
                    return
                if isinstance(reply, dict) and "status" in reply:
                    error_body = str(reply.get("body", "")).encode("utf-8")
                    self.send_response(int(reply["status"]))
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(error_body)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(error_body)
                    return
                finish_reason = reply.get("finish", "stop") if isinstance(reply, dict) else "stop"
                if isinstance(reply, dict):
                    has_image = b'"image_url"' in request_body
                    reply = reply["text"] if has_image or not reply.get("requires_image") else MOCK_NO_IMAGE_REPLY

                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Connection", "close")
                self.end_headers()
                # Deltas end mid-word and the bytes go out in chunks that end
                # mid-character, as they do with a real API under load.
                last = {"choices": [{"delta": {}, "finish_reason": finish_reason}]}
                stream = _build_event_stream(reply) + b"data: " + json.dumps(last).encode("utf-8") + b"\n\n" + b"data: [DONE]\n\n"
                for start in range(0, len(stream), MOCK_CHUNK_BYTES):
                    self.wfile.write(stream[start : start + MOCK_CHUNK_BYTES])
                    self.wfile.flush()

            def log_message(self, format: str, *args: Any) -> None:  # noqa: A002
                logger.debug("mock server: " + format, *args)

        return Handler


def _build_event_stream(text: str) -> bytes:
    """Splits a reply into SSE delta events of a few characters each."""
    events = bytearray()
    for start in range(0, len(text), MOCK_DELTA_CHARS):
        event = {"choices": [{"delta": {"content": text[start : start + MOCK_DELTA_CHARS]}}]}
        events += b"data: " + json.dumps(event, ensure_ascii=False).encode("utf-8") + b"\n\n"
    return bytes(events)


# --- Running a task ----------------------------------------------------------


def _task_environment(config: RunConfig, task: Task, task_dir: Path, mock_server: MockModelServer | None, attempt: int) -> dict[str, str]:
    env = dict(os.environ)
    env["GODOT_AI_EVAL_TASK"] = str(task_dir / "task.json")
    env["GODOT_AI_EVAL_RESULT"] = str(task_dir / "result.json")
    env["GODOT_AI_EVAL_TIMEOUT"] = str(config.timeout_sec)
    if config.knowledge_dir:
        env["GODOT_AI_KNOWLEDGE_DIR"] = config.knowledge_dir

    if mock_server is not None:
        env["GODOT_AI_EVAL_PROVIDER"] = MOCK_PROVIDER
        env["GODOT_AI_EVAL_MODEL"] = MOCK_MODEL
        env["GODOT_AI_EVAL_ENDPOINT"] = mock_server.endpoint_for(task, attempt)
        env["GODOT_AI_EVAL_API_KEY"] = MOCK_API_KEY
    elif config.provider:
        # No key here: it comes from the provider's environment variable or,
        # with --use-editor-config, from the editor's credential store.
        env["GODOT_AI_EVAL_PROVIDER"] = config.provider
        env["GODOT_AI_EVAL_MODEL"] = config.model
        env["GODOT_AI_EVAL_ENDPOINT"] = config.endpoint

    if not config.use_editor_config:
        # A private editor configuration, so the run does not depend on (or
        # touch) the developer's own settings and chat history.
        for variable, folder in (("APPDATA", "appdata"), ("LOCALAPPDATA", "localappdata"),
                                 ("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache")):
            path = task_dir / "editor" / folder
            path.mkdir(parents=True, exist_ok=True)
            env[variable] = str(path)
    return env


def run_task(config: RunConfig, task: Task, mock_server: MockModelServer | None, attempt: int = 0) -> TaskResult:
    task_dir = config.work_dir / (task.id.replace("/", "_") + (f"_run{attempt + 1}" if attempt else ""))
    project_dir = task_dir / "project"
    shutil.rmtree(task_dir, ignore_errors=True)
    shutil.copytree(PROJECT_TEMPLATE, project_dir)
    (task_dir / "task.json").write_text(json.dumps(task.data, ensure_ascii=False), encoding="utf-8")
    log_path = task_dir / "editor.log"
    result_path = task_dir / "result.json"

    command = [str(config.godot), "--headless", "--editor", "--path", str(project_dir), "--script", str(RUNNER_SCRIPT)]
    started = time.monotonic()
    status = ""
    with log_path.open("wb") as log_file:
        try:
            subprocess.run(command, env=_task_environment(config, task, task_dir, mock_server, attempt), stdout=log_file,
                           stderr=subprocess.STDOUT, timeout=config.timeout_sec + STARTUP_ALLOWANCE_SEC, check=False)
        except subprocess.TimeoutExpired:
            status = "timeout"
    duration = time.monotonic() - started

    result = TaskResult(id=task.id, category=task.category, passed=False, status=status or "crashed", duration_sec=duration)
    data: dict[str, Any] | None = None
    if result_path.exists():
        try:
            data = json.loads(result_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            result.status = "broken"  # The editor died while writing it.
    if data is not None:
        result.passed = bool(data.get("passed", False))
        result.requests = int(data.get("requests", 0))
        result.retries = int(data.get("retries", 0))
        result.failed_checks = [f"{check['type']}: {check['detail']}" for check in data.get("checks", []) if not check["passed"]]
        if data.get("error"):
            result.failed_checks.insert(0, str(data["error"]))
        result.status = "passed" if result.passed else "failed"
        result.passes = int(result.passed)
        result.first_try_passes = int(result.passed and result.retries == 0)
    else:
        result.runs = 0
        result.infra_failures = 1
    if not result.passed:
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        result.log_tail = "\n".join(lines[-LOG_TAIL_LINES:])
    if result.passed and not config.keep_projects:
        shutil.rmtree(task_dir, ignore_errors=True)
    return result


# --- Reporting ---------------------------------------------------------------


def merge_runs(runs: list[TaskResult]) -> TaskResult:
    """One result for a task from its runs. It passed only if every run that gave a verdict passed."""
    verdicts = [run for run in runs if run.runs]
    worst = next((run for run in verdicts if not run.passed), None) or (verdicts[0] if verdicts else runs[0])
    merged = TaskResult(id=worst.id, category=worst.category, passed=bool(verdicts) and all(run.passed for run in verdicts),
                        status=worst.status, failed_checks=worst.failed_checks, log_tail=worst.log_tail)
    merged.requests = sum(run.requests for run in runs)
    merged.retries = sum(run.retries for run in runs)
    merged.duration_sec = sum(run.duration_sec for run in runs)
    merged.runs = len(verdicts)
    merged.passes = sum(run.passes for run in runs)
    merged.first_try_passes = sum(run.first_try_passes for run in runs)
    merged.infra_failures = sum(run.infra_failures for run in runs)
    if not verdicts:
        merged.status = runs[0].status
    elif len(runs) > 1:
        merged.status = f"{merged.passes}/{merged.runs}"
    return merged


def summarize(results: list[TaskResult]) -> dict[str, Any]:
    total = sum(result.runs for result in results)
    passed = sum(result.passes for result in results)
    first_try = sum(result.first_try_passes for result in results)
    categories: dict[str, dict[str, int]] = {}
    for result in results:
        bucket = categories.setdefault(result.category, {"passed": 0, "total": 0})
        bucket["total"] += result.runs
        bucket["passed"] += result.passes
    return {
        "tasks": len(results),
        "infra_failures": sum(result.infra_failures for result in results),
        "total": total,
        "passed": passed,
        "pass_rate": passed / total if total else 0.0,
        "first_try": first_try,
        "first_try_rate": first_try / total if total else 0.0,
        "requests": sum(result.requests for result in results),
        "retries": sum(result.retries for result in results),
        "categories": categories,
    }


def render_report(results: list[TaskResult], summary: dict[str, Any], baseline: dict[str, Any] | None) -> str:
    lines = [f"{'task':<34} {'result':<8} {'req':>3} {'retry':>5} {'sec':>6}"]
    for result in results:
        lines.append(f"{result.id:<34} {result.status:<8} {result.requests:>3} {result.retries:>5} {result.duration_sec:>6.1f}")
        for failure in result.failed_checks:
            lines.append(f"    - {failure}")
    lines.append("")
    for category, bucket in sorted(summary["categories"].items()):
        lines.append(f"{category:<12} {bucket['passed']}/{bucket['total']}")
    lines.append("")
    lines.append(f"Passed     {summary['passed']}/{summary['total']} ({summary['pass_rate']:.0%})")
    lines.append(f"First try  {summary['first_try']}/{summary['total']} ({summary['first_try_rate']:.0%})")
    lines.append(f"Requests   {summary['requests']} ({summary['retries']} automatic retries)")
    if summary["infra_failures"]:
        without_verdict = ", ".join(f"{result.id} ({result.status})" if not result.runs else result.id for result in results if result.infra_failures)
        lines.append(f"No verdict {summary['infra_failures']} run(s), not counted above: {without_verdict}")

    if baseline is not None:
        fixed, broken = compare_with_baseline(results, baseline)
        before = {entry["id"] for entry in baseline.get("results", [])}
        now = {result.id for result in results}
        if before != now:
            lines.append("")
            lines.append(f"Note: the task sets differ ({len(now - before)} new, {len(before - now)} missing); the two rates are not directly comparable.")
        base_rate = baseline.get("summary", {}).get("pass_rate", 0.0)
        lines.append("")
        lines.append(f"Baseline   {base_rate:.0%} -> {summary['pass_rate']:.0%}")
        lines.append(f"Fixed      {', '.join(fixed) or '-'}")
        lines.append(f"Broken     {', '.join(broken) or '-'}")
    return "\n".join(lines)


def compare_with_baseline(results: list[TaskResult], baseline: dict[str, Any]) -> tuple[list[str], list[str]]:
    """Task ids that pass now but did not, and that passed but do not. Runs without a verdict are left out."""
    before = {entry["id"]: entry["passed"] for entry in baseline.get("results", []) if entry.get("runs", 1)}
    fixed = [result.id for result in results if result.runs and result.passed and before.get(result.id) is False]
    broken = [result.id for result in results if result.runs and not result.passed and before.get(result.id) is True]
    return fixed, broken


def knowledge_fingerprint(directory: Path) -> str:
    """Identifies the knowledge files a run used, so two result files can be told apart."""
    digest = hashlib.sha256()
    for path in sorted(directory.rglob("*.md")):
        digest.update(str(path.relative_to(directory)).replace("\\", "/").encode("utf-8"))
        digest.update(path.read_bytes())
    return digest.hexdigest()[:16]


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--godot", required=True, type=Path, help="Editor binary built with the ai_assistant module (the console one on Windows).")
    parser.add_argument("--mock", action="store_true", help="Replay the tasks' reference answers instead of calling a model.")
    parser.add_argument("--provider", default="", help="Provider id (anthropic, openai, gemini, glm, deepseek). Default: the editor setting.")
    parser.add_argument("--model", default="", help="Model id. Default: the provider's default.")
    parser.add_argument("--endpoint", default="", help="API endpoint override.")
    parser.add_argument("--knowledge-dir", default="", help="Folder of knowledge files to try instead of the built-in ones.")
    parser.add_argument("--filter", default="", help="Only run tasks whose id contains this text.")
    parser.add_argument("--jobs", type=int, default=2, help="Tasks to run in parallel (default: 2).")
    parser.add_argument("--repeat", type=int, default=1,
                        help="Run every task this many times (default: 1). A live model varies between runs; 3 or more gives a rate worth comparing.")
    parser.add_argument("--timeout", type=float, default=300.0, help="Seconds a task may take once the editor is up (default: 300).")
    parser.add_argument("--use-editor-config", action="store_true",
                        help="Use your real editor settings and stored API keys instead of a private configuration.")
    parser.add_argument("--out", type=Path, default=None, help="Write the results to this JSON file.")
    parser.add_argument("--baseline", type=Path, default=None,
                        help="Results file from an earlier run to compare against. Tasks that passed there and fail now make the exit code 1.")
    parser.add_argument("--allow-regressions", action="store_true", help="With --baseline: report regressions without failing.")
    parser.add_argument("--min-pass-rate", type=float, default=None, help="Exit with an error below this pass rate (0-1). Default: 1.0 with --mock.")
    parser.add_argument("--keep-projects", action="store_true", help="Keep the scratch projects of passed tasks too.")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(message)s")

    if not args.godot.exists():
        logger.error("Editor binary not found: %s", args.godot)
        return 2
    tasks = load_tasks(args.filter, include_mock_only=args.mock)
    if args.mock:
        without_reply = [task.id for task in tasks if not task.mock_replies]
        if without_reply:
            logger.error("Tasks without mock_replies: %s", ", ".join(without_reply))
            return 2
    if not tasks:
        logger.error("No tasks match.")
        return 2

    work_dir = Path(tempfile.mkdtemp(prefix="godot_ai_evals_"))
    config = RunConfig(godot=args.godot.resolve(), mock=args.mock, provider=args.provider, model=args.model,
                       endpoint=args.endpoint, knowledge_dir=str(Path(args.knowledge_dir).resolve()) if args.knowledge_dir else "",
                       timeout_sec=args.timeout, use_editor_config=args.use_editor_config,
                       keep_projects=args.keep_projects, work_dir=work_dir)

    mock_server = MockModelServer(tasks) if args.mock else None
    if mock_server is not None:
        mock_server.start()
    logger.info("Running %d tasks (%s), %d at a time. Scratch: %s", len(tasks), "mock" if args.mock else "live model", args.jobs, work_dir)

    repeat = max(1, args.repeat)

    def run_and_log(job: tuple[Task, int]) -> TaskResult:
        task, attempt = job
        result = run_task(config, task, mock_server, attempt)
        logger.info("  %-34s %s", task.id + (f" #{attempt + 1}" if repeat > 1 else ""), result.status)
        return result

    jobs = [(task, attempt) for task in tasks for attempt in range(repeat)]
    try:
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            runs = list(pool.map(run_and_log, jobs))
        results = [merge_runs(runs[index * repeat:(index + 1) * repeat]) for index in range(len(tasks))]
    finally:
        if mock_server is not None:
            mock_server.stop()

    summary = summarize(results)
    baseline = json.loads(args.baseline.read_text(encoding="utf-8")) if args.baseline else None
    sys.stdout.write("\n" + render_report(results, summary, baseline) + "\n")
    for result in results:
        if not result.passed and result.log_tail:
            logger.debug("---- %s ----\n%s", result.id, result.log_tail)

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        document = {
            "mode": "mock" if args.mock else "live",
            "provider": MOCK_PROVIDER if args.mock else args.provider,
            "model": MOCK_MODEL if args.mock else args.model,
            "knowledge_dir": config.knowledge_dir,
            # Without --knowledge-dir the editor uses what was compiled in,
            # which is this folder as of the last build.
            "knowledge": knowledge_fingerprint(Path(config.knowledge_dir) if config.knowledge_dir else MODULE_DIR / "knowledge"),
            "repeat": repeat,
            "summary": summary,
            "results": [asdict(result) for result in results],
        }
        args.out.write_text(json.dumps(document, indent=2, ensure_ascii=False), encoding="utf-8")
        logger.info("Results written to %s", args.out)
    if all(result.passed for result in results) and not args.keep_projects:
        shutil.rmtree(work_dir, ignore_errors=True)

    minimum = args.min_pass_rate if args.min_pass_rate is not None else (1.0 if args.mock else 0.0)
    if summary["pass_rate"] < minimum or (args.mock and summary["infra_failures"]):
        return 1
    if baseline is not None and not args.allow_regressions and compare_with_baseline(results, baseline)[1]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
