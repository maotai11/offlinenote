# SPDX-License-Identifier: GPL-2.0-or-later

function(_offlinenote_find_powershell out_var)
    find_program(_OFFLINENOTE_POWERSHELL NAMES pwsh powershell)
    set(${out_var} "${_OFFLINENOTE_POWERSHELL}" PARENT_SCOPE)
endfunction()

function(configure_windows_code_signing target_name)
    if(NOT WIN32)
        return()
    endif()

    _offlinenote_find_powershell(OFFLINENOTE_POWERSHELL)
    if(NOT OFFLINENOTE_POWERSHELL)
        message(STATUS "PowerShell not found; skipping code-signing targets")
        return()
    endif()

    set(OFFLINENOTE_ENABLE_CODE_SIGNING OFF CACHE BOOL
        "Sign Windows binaries after build when a certificate is configured")
    set(OFFLINENOTE_SIGN_CERT_FILE "" CACHE FILEPATH
        "Path to the PFX certificate used for Authenticode signing")
    set(OFFLINENOTE_SIGN_CERT_PASSWORD "" CACHE STRING
        "Password for OFFLINENOTE_SIGN_CERT_FILE")
    set(OFFLINENOTE_SIGNTOOL_PATH "" CACHE FILEPATH
        "Optional explicit path to signtool.exe")
    set(OFFLINENOTE_TIMESTAMP_URL "http://timestamp.digicert.com" CACHE STRING
        "RFC3161 timestamp URL used for Authenticode signing")

    set(_sign_script "${CMAKE_SOURCE_DIR}/windows-setup/sign-artifacts.ps1")
    set(_verify_script "${CMAKE_SOURCE_DIR}/windows-setup/verify-signature.ps1")

    add_custom_target(sign-${target_name}
        COMMAND "${OFFLINENOTE_POWERSHELL}" -ExecutionPolicy Bypass
            -File "${_sign_script}"
            -FilePath "$<TARGET_FILE:${target_name}>"
            -CertPath "${OFFLINENOTE_SIGN_CERT_FILE}"
            -CertPassword "${OFFLINENOTE_SIGN_CERT_PASSWORD}"
            -TimestampUrl "${OFFLINENOTE_TIMESTAMP_URL}"
            -SignToolPath "${OFFLINENOTE_SIGNTOOL_PATH}"
        DEPENDS ${target_name}
        COMMENT "Code-signing ${target_name}"
        VERBATIM
    )

    add_custom_target(verify-${target_name}-signature
        COMMAND "${OFFLINENOTE_POWERSHELL}" -ExecutionPolicy Bypass
            -File "${_verify_script}"
            -FilePath "$<TARGET_FILE:${target_name}>"
        DEPENDS ${target_name}
        COMMENT "Verifying Authenticode signature for ${target_name}"
        VERBATIM
    )

    if(OFFLINENOTE_ENABLE_CODE_SIGNING)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${OFFLINENOTE_POWERSHELL}" -ExecutionPolicy Bypass
                -File "${_sign_script}"
                -FilePath "$<TARGET_FILE:${target_name}>"
                -CertPath "${OFFLINENOTE_SIGN_CERT_FILE}"
                -CertPassword "${OFFLINENOTE_SIGN_CERT_PASSWORD}"
                -TimestampUrl "${OFFLINENOTE_TIMESTAMP_URL}"
                -SignToolPath "${OFFLINENOTE_SIGNTOOL_PATH}"
            VERBATIM
        )
    endif()
endfunction()

function(configure_fake_success_stub_audit)
    _offlinenote_find_powershell(OFFLINENOTE_POWERSHELL)
    if(NOT OFFLINENOTE_POWERSHELL)
        message(STATUS "PowerShell not found; skipping fake-success stub audit target")
        return()
    endif()

    add_custom_target(audit-fake-success-stubs
        COMMAND "${OFFLINENOTE_POWERSHELL}" -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/audit-fake-success-stubs.ps1"
            -CMakeFile "${CMAKE_SOURCE_DIR}/CMakeLists.txt"
        COMMENT "Auditing build sources for excluded fake-success stubs"
        VERBATIM
    )
endfunction()
