#pragma once

// How a notice shown in the chat is to be read. Shared by everything that
// reports to the user, whichever layer it is in.
enum AINoticeLevel {
	AI_NOTICE_INFO,
	AI_NOTICE_SUCCESS,
	AI_NOTICE_WARNING,
	AI_NOTICE_ERROR,
};
