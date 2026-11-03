# Host dependencies for the pinned standalone Impeller SDK.
solutions = [{
    'name': '.',
    'url': 'https://github.com/flutter/flutter.git',
    'deps_file': 'DEPS',
    'managed': False,
    'custom_deps': {},
    'custom_vars': {
        'download_android_deps': False,
        'download_fuchsia_deps': False,
        'download_esbuild': False,
        'setup_githooks': False,
    },
}]
