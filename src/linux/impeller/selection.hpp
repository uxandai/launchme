#pragma once
// Linux renderer selection; kept free of graphics-library headers for the app layer.
namespace lm_impeller {
bool compiled();
// Applied before creating the first window. LAUNCHME_IMPELLER=0/1 overrides the saved value.
void request(bool enabled);
bool requested();
}
