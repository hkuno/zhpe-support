add_library(zhpe_stats SHARED libzhpe_stats.c)
target_link_libraries(zhpe_stats PUBLIC zhpeq_util simh )
# cmake v3.13 or later: target_link_options(zhpe_stats PRIVATE -znow)
set_target_properties(zhpe_stats PROPERTIES LINK_FLAGS -znow)

install(TARGETS zhpe_stats DESTINATION lib)
install(FILES
  ${CMAKE_SOURCE_DIR}/include/zhpe_stats.h
  ${CMAKE_SOURCE_DIR}/include/zhpe_stats_types.h
  DESTINATION include)
install(PROGRAMS unpackdata.py DESTINATION libexec)

add_executable(calibrate_stats calibrate_stats.c)
set_target_properties(calibrate_stats PROPERTIES LINK_FLAGS " -DHAVE_ZHPE_STATS")
target_link_libraries(
  calibrate_stats PUBLIC zhpe_stats zhpeq_util)

install(TARGETS calibrate_stats DESTINATION libexec)

add_executable(stat_pthread_spinlock stat_pthread_spinlock.c)
set_target_properties(stat_pthread_spinlock PROPERTIES LINK_FLAGS " -DHAVE_ZHPE_STATS")
target_link_libraries(
  stat_pthread_spinlock PUBLIC zhpe_stats zhpeq_util)

install(TARGETS stat_pthread_spinlock DESTINATION libexec)
