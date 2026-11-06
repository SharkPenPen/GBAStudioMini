# cmake/psp-package.cmake
#
# 把 game_psp.elf 打包成 EBOOT.PBP。
# 需要 pspdev 里的 psp-fixup-imports / mksfo / pack-pbp。
function(psp_package target)
  if(NOT PSPDEV)
    message(FATAL_ERROR "PSPDEV is not set")
  endif()

  set(psp_sfo "${CMAKE_BINARY_DIR}/PARAM.SFO")
  set(psp_eboot "${CMAKE_BINARY_DIR}/EBOOT.PBP")

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${PSPDEV}/bin/psp-fixup-imports" "$<TARGET_FILE:${target}>"
    COMMAND "${PSPDEV}/bin/mksfo" "PocketLoom" "${psp_sfo}"
    COMMAND "${PSPDEV}/bin/pack-pbp"
            "${psp_eboot}"
            "${psp_sfo}"
            NULL NULL NULL NULL NULL
            "$<TARGET_FILE:${target}>"
            NULL
    COMMENT "Packaging PSP EBOOT.PBP"
    VERBATIM)

endfunction()
