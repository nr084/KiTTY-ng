set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_sources_from_current_dir(utils
  kittyng.c
  kittyng-utility-ui.c
  kittyng-companions.c
  kittyng-portable.c
  kittyng-keypaths.c
  kittyng-extensions.c
  kittyng-legacy-crypt.c
  utils/agent_mutex_name.c
  utils/agent_named_pipe_name.c
  utils/arm_arch_queries.c
  utils/aux_match_opt.c
  utils/blink_time.c
  utils/centre_window.c
  utils/cmdline_arg.c
  utils/cryptoapi.c
  utils/defaults.c
  utils/dll_hijacking_protection.c
  utils/dputs.c
  utils/escape_registry_key.c
  utils/filename.c
  utils/fontspec.c
  utils/getdlgitemtext_alloc.c
  utils/get_system_dir.c
  utils/get_username.c
  utils/gui-timing.c
  utils/interprocess_mutex.c
  utils/is_console_handle.c
  utils/load_system32_dll.c
  utils/ltime.c
  utils/makedlgitemborderless.c
  utils/make_spr_sw_abort_winerror.c
  utils/message_box.c
  utils/minefield.c
  utils/open_for_write_would_lose_data.c
  utils/pgp_fingerprints_msgbox.c
  utils/platform_get_x_display.c
  utils/registry.c
  utils/request_file.c
  utils/screenshot.c
  utils/security.c
  utils/shinydialogbox.c
  utils/split_into_argv.c
  utils/split_into_argv_w.c
  utils/subprocess_waiter.c
  utils/version.c
  utils/win_strerror.c
  unicode.c)
target_link_libraries(utils PUBLIC bcrypt)

add_executable(test_kittyng ${CMAKE_SOURCE_DIR}/test/test_kittyng.c storage.c)
target_link_libraries(test_kittyng utils ${platform_libraries})
add_executable(test_key_paths ${CMAKE_SOURCE_DIR}/test/test_key_paths.c storage.c)
target_link_libraries(test_key_paths utils ${platform_libraries})
add_executable(test_extensions ${CMAKE_SOURCE_DIR}/test/test_extensions.c ${CMAKE_SOURCE_DIR}/stubs/no-gss.c)
target_link_libraries(test_extensions settings utils ${platform_libraries})
add_executable(test_session_browser ${CMAKE_SOURCE_DIR}/test/test_session_browser.c)
target_link_libraries(test_session_browser utils ${platform_libraries})
add_executable(test_utility_ui ${CMAKE_SOURCE_DIR}/test/test_utility_ui.c)
target_link_libraries(test_utility_ui utils ${platform_libraries})
add_executable(test_language_controls ${CMAKE_SOURCE_DIR}/test/test_language_controls.c
  kittyng-language.c ${CMAKE_SOURCE_DIR}/dialog.c)
target_link_libraries(test_language_controls utils ${platform_libraries})
if(MSVC)
  target_compile_options(test_language_controls PRIVATE /Gy)
  target_link_options(test_language_controls PRIVATE /OPT:REF)
else()
  target_compile_options(test_language_controls PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(test_language_controls PRIVATE -Wl,--gc-sections)
endif()
add_executable(test_companion_probe WIN32 ${CMAKE_SOURCE_DIR}/test/test_companion_probe.c)
target_link_libraries(test_companion_probe PRIVATE shell32)
add_executable(test_kittyng_legacy
  ${CMAKE_SOURCE_DIR}/test/test_kittyng_legacy.c kittyng-legacy-crypt.c)
if(NOT HAVE_STRTOUMAX)
  add_sources_from_current_dir(utils utils/strtoumax.c)
endif()
add_sources_from_current_dir(eventloop
  cliloop.c handle-wait.c)
add_sources_from_current_dir(console
  select-cli.c nohelp.c console.c)
add_sources_from_current_dir(settings
  storage.c)
add_sources_from_current_dir(network
  network.c handle-socket.c named-pipe-client.c named-pipe-server.c local-proxy.c x11.c)
add_sources_from_current_dir(sshcommon
  noise.c)
add_sources_from_current_dir(sshclient
  agent-client.c gss.c sharing.c)
add_sources_from_current_dir(sftpclient
  sftp.c)
add_sources_from_current_dir(otherbackends
  serial.c)
add_sources_from_current_dir(agent
  agent-client.c)
add_sources_from_current_dir(guiterminal
  dialog.c controls.c config.c printing.c jump-list.c sizetip.c kittyng-transfer.c kittyng-language.c kittyng-proxy-ui.c)
add_dependencies(guiterminal generated_licence_h) # dialog.c uses licence.h

# This object awkwardly needs to live in the network library as well
# as the eventloop library, in case it didn't get pulled in from the
# latter before handle-socket.c needed it.
add_library(handle-io OBJECT
  handle-io.c)
target_sources(eventloop PRIVATE $<TARGET_OBJECTS:handle-io>)
target_sources(network PRIVATE $<TARGET_OBJECTS:handle-io>)

add_library(guimisc STATIC
  select-gui.c)

add_executable(pageant
  pageant.c
  help.c
  pageant.rc)
add_dependencies(pageant generated_licence_h)
target_link_libraries(pageant
  guimisc eventloop agent network crypto utils
  ${platform_libraries})
set_target_properties(pageant PROPERTIES WIN32_EXECUTABLE ON)
installed_program(pageant)

add_sources_from_current_dir(plink no-jump-list.c nohelp.c plink.rc)
add_dependencies(plink generated_licence_h)

add_sources_from_current_dir(pscp no-jump-list.c nohelp.c pscp.rc)
add_dependencies(pscp generated_licence_h)

add_sources_from_current_dir(psftp no-jump-list.c nohelp.c psftp.rc)
add_dependencies(psftp generated_licence_h)

add_sources_from_current_dir(psocks nohelp.c)

add_executable(kitty-ng
  window.c
  putty.c
  help.c
  ${CMAKE_SOURCE_DIR}/stubs/no-console.c
  putty.rc)
set_property(SOURCE putty.rc APPEND PROPERTY OBJECT_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/putty-common.rc2
  ${CMAKE_CURRENT_SOURCE_DIR}/version.rc2
  ${CMAKE_CURRENT_SOURCE_DIR}/kitty-ng.ico
  ${CMAKE_SOURCE_DIR}/version.h)
be_list(kitty-ng KiTTY-NG SSH SERIAL OTHERBACKENDS)
add_dependencies(kitty-ng generated_licence_h)

# Only the output filename varies; keep the upstream-compatible target name.
if(CMAKE_GENERATOR_PLATFORM MATCHES "^[Aa][Rr][Mm]64$" OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|ARM64|arm64)$" OR
   CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64" OR
   CMAKE_C_COMPILER MATCHES "aarch64-w64-mingw32")
  set(kitty_ng_output_arch arm64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(kitty_ng_output_arch x64)
else()
  set(kitty_ng_output_arch x86)
endif()
set_target_properties(kitty-ng PROPERTIES OUTPUT_NAME "kitty-ng-${kitty_ng_output_arch}")
target_link_libraries(kitty-ng
  guiterminal guimisc eventloop sshclient otherbackends settings network crypto
  utils
  ${platform_libraries})
set_target_properties(kitty-ng PROPERTIES WIN32_EXECUTABLE ON)
installed_program(kitty-ng)

add_executable(puttytel
  window.c
  putty.c
  help.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
  ${CMAKE_SOURCE_DIR}/stubs/no-console.c
  ${CMAKE_SOURCE_DIR}/stubs/no-rand.c
  ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
  ${CMAKE_SOURCE_DIR}/proxy/nocproxy.c
  ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
  puttytel.rc)
be_list(puttytel PuTTYtel SERIAL OTHERBACKENDS)
add_dependencies(puttytel generated_licence_h)
target_link_libraries(puttytel
  guiterminal guimisc eventloop otherbackends settings network utils
  ${platform_libraries})
set_target_properties(puttytel PROPERTIES WIN32_EXECUTABLE ON)
installed_program(puttytel)

add_executable(puttygen
  puttygen.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  noise.c
  no-jump-list.c
  storage.c
  help.c
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c
  controls.c
  puttygen.rc)
add_dependencies(puttygen generated_licence_h)
target_link_libraries(puttygen
  keygen guimisc crypto utils
  ${platform_libraries})
set_target_properties(puttygen PROPERTIES WIN32_EXECUTABLE ON)
installed_program(puttygen)

if(HAVE_CONPTY)
  add_executable(pterm
    window.c
    pterm.c
    help.c
    conpty.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c
    ${CMAKE_SOURCE_DIR}/stubs/no-rand.c
    ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
    ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
    pterm.rc)
  be_list(pterm pterm)
  add_dependencies(pterm generated_licence_h)
  target_link_libraries(pterm
    guiterminal guimisc eventloop settings network utils
    ${platform_libraries})
  set_target_properties(pterm PROPERTIES WIN32_EXECUTABLE ON)
  installed_program(pterm)
else()
  message("ConPTY not available; cannot build Windows pterm")
endif()

add_executable(test_split_into_argv
  test/test_split_into_argv.c)
target_compile_definitions(test_split_into_argv PRIVATE TEST)
target_link_libraries(test_split_into_argv utils ${platform_libraries})

add_executable(test_screenshot
  test/test_screenshot.c)
target_link_libraries(test_screenshot utils ${platform_libraries})

add_executable(test_lineedit
  ${CMAKE_SOURCE_DIR}/test/test_lineedit.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/stubs/no-logging.c
  ${CMAKE_SOURCE_DIR}/stubs/no-printing.c
  ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  no-jump-list.c)
target_link_libraries(test_lineedit
  guiterminal settings eventloop utils ${platform_libraries})

add_executable(test_terminal
  ${CMAKE_SOURCE_DIR}/test/test_terminal.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  no-jump-list.c)
target_link_libraries(test_terminal
  guiterminal settings eventloop utils ${platform_libraries})

add_sources_from_current_dir(test_conf no-jump-list.c handle-wait.c)
foreach(program pageant puttygen puttytel)
  set_property(SOURCE ${program}.rc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/version.rc2 ${CMAKE_SOURCE_DIR}/version.h)
endforeach()

# KiTTY utility filenames include their architecture, without release numbers.
set_target_properties(plink PROPERTIES OUTPUT_NAME "klink-${kitty_ng_output_arch}")
set_target_properties(pscp PROPERTIES OUTPUT_NAME "kscp-${kitty_ng_output_arch}")
set_target_properties(psftp PROPERTIES OUTPUT_NAME "ksftp-${kitty_ng_output_arch}")
set_target_properties(puttygen PROPERTIES OUTPUT_NAME "kittygen-${kitty_ng_output_arch}")
set_target_properties(pageant PROPERTIES OUTPUT_NAME "kageant-${kitty_ng_output_arch}")
