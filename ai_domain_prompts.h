#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"

// Domain-specific prompt injection system.
// Detects the user's intent from their message and injects specialized
// API patterns so the AI generates correct code for that domain.
// Inspired by godot-mcp-pro's 162 discrete tools — we teach the AI
// the same patterns via contextual prompt injection.
class AIDomainPrompts {
public:
	enum Domain {
		DOMAIN_ANIMATION,    // AnimationTree, StateMachine, BlendSpace
		DOMAIN_TILEMAP,      // TileMap / TileSet batch operations
		DOMAIN_NAVIGATION,   // NavigationRegion, NavigationAgent, path-finding
		DOMAIN_PARTICLES,    // GPUParticles2D/3D, CPUParticles, materials
		DOMAIN_SHADER,       // Shader creation, ShaderMaterial, uniforms
		DOMAIN_PHYSICS,      // RigidBody, collision shapes/layers, joints, raycasts
		DOMAIN_BATCH,        // Cross-scene operations, bulk property edits
		DOMAIN_AUDIO,        // AudioStreamPlayer, buses, effects
		DOMAIN_3D_SCENE,     // Mesh, Camera, Light, Environment, GridMap
		DOMAIN_SCREENSHOT,       // Viewport capture
		DOMAIN_COMPLEX_PROJECT,  // Risk-first decomposition for full games
		DOMAIN_MAX,
	};

	// Detect which domains are relevant to the user message.
	static Vector<Domain> detect_domains(const String &p_message);

	// Get the prompt fragment for a specific domain.
	static String get_domain_prompt(Domain p_domain);

	// Build combined prompt for all detected domains.
	static String build_contextual_prompt(const String &p_message);

private:
	static bool _matches_keywords(const String &p_lower, const char *const *p_keywords);
};

#endif // TOOLS_ENABLED
