#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
application="${2:-}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--host|--firmware] [notification-hub]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    run_host_tests
}

run_host_tests() {
    local test_dir
    local dead_strip
    dead_strip="-Wl,--gc-sections"
    if [[ "$(uname -s)" == Darwin ]]; then dead_strip="-Wl,-dead_strip"; fi
    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${dead_strip}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    for test in protocol backlog form settings archive alert access web_auth; do
        local sources="apps/notification-hub/firmware/main/hub_${test}.c"
        if [[ "$test" == form || "$test" == web_auth ]]; then sources="apps/notification-hub/firmware/main/hub_form.c apps/notification-hub/firmware/main/hub_alert.c apps/notification-hub/firmware/main/hub_web_auth.c"; fi
        if [[ "$test" == settings ]]; then sources="apps/notification-hub/firmware/main/hub_alert.c"; fi
        if [[ "$test" == archive ]]; then sources="apps/notification-hub/firmware/main/hub_protocol.c"; fi
        "${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
            -Iapps/notification-hub/tests/stubs -Iapps/notification-hub/firmware/main \
            "apps/notification-hub/tests/test_hub_${test}.c" ${sources} -lm \
            -o "${test_dir}/test_hub_${test}"
        "${test_dir}/test_hub_${test}"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Iapps/notification-hub/tests/ui_stubs -Iapps/notification-hub/firmware/main \
        apps/notification-hub/tests/test_hub_ui.c apps/notification-hub/firmware/main/hub_ui.c \
        -o "${test_dir}/test_hub_ui"
    "${test_dir}/test_hub_ui"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Iapps/notification-hub/firmware/main \
        apps/notification-hub/tests/test_hub_catalog.c \
        -o "${test_dir}/test_hub_catalog"
    "${test_dir}/test_hub_catalog"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Iapps/notification-hub/tests/sound_stubs -Iapps/notification-hub/tests/stubs \
        -Iapps/notification-hub/firmware/main apps/notification-hub/tests/test_hub_sound.c \
        apps/notification-hub/firmware/main/hub_alert.c -lm -o "${test_dir}/test_hub_sound"
    "${test_dir}/test_hub_sound"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iapps/notification-hub/firmware/main \
        apps/notification-hub/tests/test_hub_response.c -o "${test_dir}/test_hub_response"
    "${test_dir}/test_hub_response"
    PYTHONDONTWRITEBYTECODE=1 python3 apps/notification-hub/tests/test_font_coverage.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${project_dir}/sdkconfig.defaults" \
        idf.py -C "${project_dir}" -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -C "${project_dir}" -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${output_dir}/firmware"
    mkdir -p "${output_dir}"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${output_dir}/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

project_dir="${repo_root}"
output_dir="${repo_root}/build"
case "${application}" in
    "") ;;
    notification-hub)
        project_dir="${repo_root}/apps/notification-hub/firmware"
        output_dir="${repo_root}/build/notification-hub"
        ;;
    *) usage; exit 2 ;;
esac
cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --host)
        run_host_tests
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
