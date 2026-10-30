# libsamplerate (NC_DEPS/libsamplerate, libsndfile/libsamplerate 0.2.2,
# BSD-2-Clause): the voices' band-limited sinc resampler, the
# hakux_audio_resampler option's "sinc" (dev/apu/vp/resample_sinc.c). Its
# SRC_SINC_MEDIUM_QUALITY converter only, without its config.h. Without it the
# voices resample linearly whatever the option says.
set(NC_SAMPLERATE "${NC_DEPS}/libsamplerate")
if(EXISTS "${NC_SAMPLERATE}/src/samplerate.c")
  add_library(nc_samplerate STATIC
    "${NC_SAMPLERATE}/src/samplerate.c"
    "${NC_SAMPLERATE}/src/src_linear.c"
    "${NC_SAMPLERATE}/src/src_sinc.c"
    "${NC_SAMPLERATE}/src/src_zoh.c"
  )
  target_include_directories(nc_samplerate PUBLIC "${NC_SAMPLERATE}/include")
  target_compile_definitions(nc_samplerate PRIVATE ENABLE_SINC_MEDIUM_CONVERTER HAVE_STDBOOL_H PACKAGE="libsamplerate"
                             VERSION="0.2.2")
  target_compile_options(nc_samplerate PRIVATE -w)
  set_target_properties(nc_samplerate PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
  if(UNIX)
    target_link_libraries(nc_samplerate PRIVATE m)
  endif()
  target_sources(hakux_apu PRIVATE "${NC_APU}/vp/resample_sinc.c")
  target_link_libraries(hakux_apu PRIVATE nc_samplerate)
  target_compile_definitions(hakux_apu PRIVATE NC_HAVE_SINC)
else()
  message(STATUS "libsamplerate not in ${NC_SAMPLERATE}: the voices resample linearly")
endif()
