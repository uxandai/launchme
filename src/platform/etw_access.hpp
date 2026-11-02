#pragma once

// Who may start a real-time ETW session (the FPS widget's PresentMon trace): administrators and
// members of "Performance Log Users" (S-1-5-32-559). Launchme itself runs as a plain user, so
// the widget needs the user in that group once instead of the whole launcher running elevated.
namespace etw_access {

enum class State {
    allowed,      // This process may trace now (elevated, or the group is in its token).
    pending_sign_in, // The user is in the group already, but this session's token predates it.
    missing,      // Not in the group.
    unknown,
};

State state();

// One UAC prompt: adds the current user to Performance Log Users. The group reaches the token at
// the next sign-in. Returns when the prompt is answered, without waiting for the change; false when
// it was declined or the command could not start.
bool grant();

}
