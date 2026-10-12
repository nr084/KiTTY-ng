if(APPLE)
  add_compile_definitions(KITTY_NG_MACOS)
  include_directories(${CMAKE_SOURCE_DIR}/macos)
  set(KITTY_NG_STORAGE ../macos/storage.c)
  target_link_libraries(utils "-framework CoreFoundation")
  add_sources_from_current_dir(utils ../macos/cli-language.c ../macos/helper.c ../macos/snapshot.c ../windows/kittyng-legacy-crypt.c ../windows/utils/escape_registry_key.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_compile_definitions(KITTY_NG_LINUX)
  include_directories(${CMAKE_SOURCE_DIR}/macos ${CMAKE_SOURCE_DIR}/linux)
  set(KITTY_NG_STORAGE ../macos/storage.c)
  add_sources_from_current_dir(utils ../linux/language.c ../macos/helper.c ../macos/snapshot.c ../windows/kittyng-legacy-crypt.c ../windows/utils/escape_registry_key.c)
else()
  set(KITTY_NG_STORAGE storage.c)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_sources_from_current_dir(utils
  utils/arm_arch_queries.c
  utils/block_signal.c
  utils/cloexec.c
  utils/cmdline_arg.c
  utils/dputs.c
  utils/filename.c
  utils/fontspec.c
  utils/getticks.c
  utils/get_username.c
  utils/keysym_to_unicode.c
  utils/make_dir_and_check_ours.c
  utils/make_dir_path.c
  utils/make_spr_sw_abort_errno.c
  utils/nonblock.c
  utils/open_for_write_would_lose_data.c
  utils/pgp_fingerprints.c
  utils/pollwrap.c
  utils/signal.c
  utils/subprocess_waiter.c
  utils/x11_ignore_error.c
  # We want the ISO C implementation of ltime(), because we don't have
  # a local better alternative
  ../utils/ltime.c)
# Compiled icon pixmap files
add_library(puttyxpms STATIC
  putty-xpm.c
  putty-rgb.c
  putty-config-xpm.c
  putty-config-rgb.c)
add_library(ptermxpms STATIC
  pterm-xpm.c
  pterm-rgb.c
  pterm-config-xpm.c
  pterm-config-rgb.c)
add_sources_from_current_dir(eventloop
  cliloop.c uxsel.c)
add_sources_from_current_dir(console
  console.c)
add_sources_from_current_dir(settings
  ${KITTY_NG_STORAGE})
add_sources_from_current_dir(network
  network.c fd-socket.c agent-socket.c peerinfo.c local-proxy.c x11.c)
add_sources_from_current_dir(sshcommon
  noise.c)
add_sources_from_current_dir(sshclient
  gss.c agent-client.c sharing.c)
add_sources_from_current_dir(sshserver
  sftpserver.c procnet.c)
add_sources_from_current_dir(sftpclient
  sftp.c)
add_sources_from_current_dir(otherbackends
  serial.c)
add_sources_from_current_dir(agent
  agent-client.c)

add_executable(fuzzterm
  ${CMAKE_SOURCE_DIR}/test/fuzzterm.c
  ${CMAKE_SOURCE_DIR}/stubs/no-print.c
  unicode.c
  no-gtk.c
  $<TARGET_OBJECTS:logging>)
be_list(fuzzterm FuZZterm)
add_dependencies(fuzzterm generated_licence_h)
target_link_libraries(fuzzterm
  guiterminal eventloop charset settings utils)

add_executable(osxlaunch
  osxlaunch.c)

add_sources_from_current_dir(plink unicode.c no-gtk.c)
add_sources_from_current_dir(pscp unicode.c no-gtk.c)
add_sources_from_current_dir(psftp unicode.c no-gtk.c)
add_sources_from_current_dir(psocks no-gtk.c)

add_executable(psusan
  psusan.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  ${CMAKE_SOURCE_DIR}/ssh/scpserver.c
  unicode.c
  no-gtk.c
  pty.c)
be_list(psusan psusan)
target_link_libraries(psusan
  eventloop sshserver keygen settings network crypto utils)
installed_program(psusan)

add_library(puttygen-common OBJECT
  ${CMAKE_SOURCE_DIR}/stubs/no-timing.c
  keygen-noise.c
  no-gtk.c
  noise.c
  ${KITTY_NG_STORAGE}
  ${CMAKE_SOURCE_DIR}/sshpubk.c
  ${CMAKE_SOURCE_DIR}/sshrand.c)

add_executable(puttygen
  ${CMAKE_SOURCE_DIR}/cmdgen.c
  $<TARGET_OBJECTS:puttygen-common>)
target_link_libraries(puttygen keygen console crypto utils)
installed_program(puttygen)

add_executable(cgtest
  ${CMAKE_SOURCE_DIR}/cgtest.c
  $<TARGET_OBJECTS:puttygen-common>)
target_link_libraries(cgtest keygen console crypto utils)

if(HAVE_ALIGNED_ALLOC)
  add_library(overaligned_alloc OBJECT
    ${CMAKE_SOURCE_DIR}/utils/memory.c)
  target_compile_definitions(overaligned_alloc PRIVATE ALLOCATION_ALIGNMENT=128)
  add_executable(testsc
    ${CMAKE_SOURCE_DIR}/test/testsc.c
    ${CMAKE_SOURCE_DIR}/test/test_random.c
    $<TARGET_OBJECTS:overaligned_alloc>)
  target_link_libraries(testsc keygen crypto utils)
endif()

add_executable(testzlib
  ${CMAKE_SOURCE_DIR}/test/testzlib.c
  ${CMAKE_SOURCE_DIR}/ssh/zlib.c)
target_link_libraries(testzlib utils)

add_executable(uppity
  uppity.c
  ${CMAKE_SOURCE_DIR}/ssh/scpserver.c
  no-gtk.c
  pty.c
  unicode.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c)
be_list(uppity Uppity)
target_link_libraries(uppity
  eventloop sshserver keygen settings network crypto utils)

if(GTK_FOUND)
  add_sources_from_current_dir(utils
    utils/align_label_left.c
    utils/buildinfo_gtk_version.c
    utils/get_label_text_dimensions.c
    utils/get_x11_display.c
    utils/our_dialog.c
    utils/string_width.c
    columns.c)
  add_sources_from_current_dir(guiterminal
    window.c unifont.c dialog.c config-gtk.c gtk-common.c config-unix.c unicode.c printing.c)
  add_dependencies(guiterminal generated_licence_h) # dialog.c uses licence.h

  add_executable(pterm
    pterm.c
    main-gtk-simple.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c
    ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
    ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
    pty.c)
  be_list(pterm pterm)
  target_link_libraries(pterm
    guiterminal eventloop settings utils ptermxpms
    ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  installed_program(pterm)

  if(GTK_VERSION GREATER_EQUAL 3)
    add_executable(ptermapp
      pterm.c
      main-gtk-application.c
      ${CMAKE_SOURCE_DIR}/stubs/no-cmdline.c
      ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
      ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
      ${CMAKE_SOURCE_DIR}/stubs/no-console.c
      ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
      ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
      pty.c)
    be_list(ptermapp pterm)
    target_link_libraries(ptermapp
      guiterminal eventloop settings utils ptermxpms
      ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  endif()

  add_executable(putty
    putty.c
    main-gtk-simple.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(guiterminal PRIVATE ../linux/gui.c ../linux/tools-gui.c ../linux/preferences.c ../linux/transfer.c ../linux/icons.c ../macos/automation.c)
    # Portable settings preserve encrypted legacy passwords even for auxiliary
    # Unix frontends, so storage carries this dependency explicitly.
    target_sources(settings PRIVATE ../sshpubk.c)
    target_link_libraries(settings PRIVATE crypto)
  endif()
  be_list(putty KiTTY-ng SSH SERIAL OTHERBACKENDS)
  target_link_libraries(putty
    guiterminal eventloop sshclient otherbackends settings
    network crypto utils puttyxpms
    ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  set_target_properties(putty
    PROPERTIES LINK_INTERFACE_MULTIPLICITY 2)
  installed_program(putty)

  if(GTK_VERSION GREATER_EQUAL 3)
    add_executable(puttyapp
      putty.c
      main-gtk-application.c
      ${CMAKE_SOURCE_DIR}/stubs/no-cmdline.c
      ${CMAKE_SOURCE_DIR}/stubs/no-console.c)
    be_list(puttyapp PuTTY SSH SERIAL OTHERBACKENDS)
    target_link_libraries(puttyapp
      guiterminal eventloop sshclient otherbackends settings
      network crypto utils puttyxpms
      ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})
  endif()

  add_executable(puttytel
    putty.c
    main-gtk-simple.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-ca-config.c
    ${CMAKE_SOURCE_DIR}/stubs/no-console.c
    ${CMAKE_SOURCE_DIR}/stubs/no-rand.c
    ${CMAKE_SOURCE_DIR}/stubs/no-dit.c
    ${CMAKE_SOURCE_DIR}/proxy/nocproxy.c
    ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c)
  be_list(puttytel PuTTYtel SERIAL OTHERBACKENDS)
  target_link_libraries(puttytel
    guiterminal eventloop otherbackends settings network utils
    puttyxpms
    ${GTK_LIBRARIES} ${X11_LIBRARIES} ${X11_Xrender_LIB})

  add_executable(test_lineedit
    ${CMAKE_SOURCE_DIR}/test/test_lineedit.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-logging.c
    ${CMAKE_SOURCE_DIR}/stubs/no-printing.c
    ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
    ${CMAKE_SOURCE_DIR}/stubs/no-timing.c)
  target_link_libraries(test_lineedit
    guiterminal settings eventloop utils ${platform_libraries})

  add_executable(test_terminal
    ${CMAKE_SOURCE_DIR}/test/test_terminal.c
    ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
    ${CMAKE_SOURCE_DIR}/stubs/no-storage.c
    ${CMAKE_SOURCE_DIR}/stubs/no-timing.c)
  target_link_libraries(test_terminal
    guiterminal settings eventloop utils ${platform_libraries})
endif()

# Pageant is built whether we have GTK or not; in its absence we
# degrade to a version that doesn't provide the GTK askpass.
if(GTK_FOUND)
  set(pageant_conditional_sources askpass.c)
  set(pageant_libs ${GTK_LIBRARIES})
else()
  set(pageant_conditional_sources noaskpass.c no-gtk.c)
  set(pageant_libs)
endif()
add_executable(pageant
  pageant.c
  ${CMAKE_SOURCE_DIR}/stubs/no-gss.c
  x11.c
  noise.c
  ${CMAKE_SOURCE_DIR}/ssh/x11fwd.c
  ${CMAKE_SOURCE_DIR}/proxy/nosshproxy.c
  ${pageant_conditional_sources})
be_list(pageant Pageant)
target_link_libraries(pageant
  eventloop console agent settings network crypto utils
  ${pageant_libs})
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_sources_from_current_dir(pageant unicode.c)
endif()
installed_program(pageant)

add_sources_from_current_dir(test_conf unicode.c stubs/no-uxsel.c)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(pair "putty;kitty-ng" "plink;klink" "pscp;kscp" "psftp;ksftp" "puttygen;kittygen" "pageant;kageant")
    list(GET pair 0 target)
    list(GET pair 1 name)
    if(TARGET ${target})
      set_target_properties(${target} PROPERTIES OUTPUT_NAME ${name})
    endif()
  endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(storage_test test_conf test_lineedit test_terminal)
    if(TARGET ${storage_test})
      target_sources(${storage_test} PRIVATE ../linux/test-storage.c)
    endif()
  endforeach()
  # CA parsing in the shared portable store uses the common public-key parser.
  target_sources(fuzzterm PRIVATE ../stubs/no-storage.c ../linux/test-storage.c)
  target_sources(psocks PRIVATE ../linux/test-storage.c)
endif()
