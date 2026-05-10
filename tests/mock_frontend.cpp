#include <obs-frontend-api.h>
#include <obs.h>
#include <vector>
#include <util/bmem.h>

// Global state for mocks
obs_source_t* g_current_scene = nullptr;
obs_source_t* g_current_preview_scene = nullptr;
obs_source_t* g_current_transition = nullptr;
std::vector<obs_source_t*> g_transitions;

extern "C" {

void obs_frontend_get_scenes(struct obs_frontend_source_list *source_list) {
	if (!source_list) return;
	// For tests, return an empty scene list.
	// Tests can populate this if needed via a helper function.
	source_list->sources.num = 0;
	source_list->sources.array = nullptr;
}

void obs_frontend_get_transitions(struct obs_frontend_source_list *source_list) {
    if (!source_list) return;
    source_list->sources.num = g_transitions.size();
    source_list->sources.array = (obs_source_t**)bmalloc(sizeof(obs_source_t*) * g_transitions.size());
    for (size_t i = 0; i < g_transitions.size(); i++) {
        source_list->sources.array[i] = g_transitions[i];
        obs_source_get_ref(g_transitions[i]);
    }
}

// obs_frontend_source_list_free is defined inline in the header in some versions or we are seeing it being pulled in?
// The error says "previously defined here".
// If it's inline in the header, we don't need to define it.
// We just need to make sure our mock implementation of obs_frontend_get_transitions sets up the struct correctly.
// The header version uses da_free which uses bfree.

obs_source_t *obs_frontend_get_current_scene(void) {
    if (g_current_scene) obs_source_get_ref(g_current_scene);
    return g_current_scene;
}

void obs_frontend_set_current_scene(obs_source_t *scene) {
    if (g_current_scene) obs_source_release(g_current_scene);
    g_current_scene = scene;
    if (g_current_scene) obs_source_get_ref(g_current_scene);
}

obs_source_t *obs_frontend_get_current_preview_scene(void) {
    if (g_current_preview_scene) obs_source_get_ref(g_current_preview_scene);
    return g_current_preview_scene;
}

void obs_frontend_set_current_preview_scene(obs_source_t *scene) {
    if (g_current_preview_scene) obs_source_release(g_current_preview_scene);
    g_current_preview_scene = scene;
    if (g_current_preview_scene) obs_source_get_ref(g_current_preview_scene);
}

obs_source_t *obs_frontend_get_current_transition(void) {
    if (g_current_transition) obs_source_get_ref(g_current_transition);
    return g_current_transition;
}

void obs_frontend_set_current_transition(obs_source_t *transition) {
    if (g_current_transition) obs_source_release(g_current_transition);
    g_current_transition = transition;
    if (g_current_transition) obs_source_get_ref(g_current_transition);
}

bool obs_frontend_preview_program_mode_active(void) {
    // Default to non-studio-mode for tests
    return false;
}

bool obs_frontend_add_dock_by_id(const char *id, const char *title, void *dock) {
    // No-op for test
    return true;
}

} // extern "C"
