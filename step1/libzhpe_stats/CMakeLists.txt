add_library(zhpe_stats SHARED libzhpe_stats.c)
target_link_libraries(zhpe_stats PUBLIC zhpeq_util simh -lm)
# cmake v3.13 or later: target_link_options(zhpe_stats PRIVATE -znow)
set_target_properties(zhpe_stats PROPERTIES LINK_FLAGS -znow)

install(TARGETS zhpe_stats DESTINATION lib)
install(FILES
  ${CMAKE_SOURCE_DIR}/include/zhpe_stats.h
  ${CMAKE_SOURCE_DIR}/include/zhpe_stats_types.h
  DESTINATION include)
install(PROGRAMS unpackdata.py DESTINATION libexec)

add_executable(sanitycheck_stats sanitycheck_stats.c)
set_target_properties(sanitycheck_stats PROPERTIES LINK_FLAGS " -DHAVE_ZHPE_STATS")
target_link_libraries(
  sanitycheck_stats PUBLIC zhpe_stats zhpeq_util)
install(TARGETS sanitycheck_stats DESTINATION libexec)

add_executable(stat_overheads stat_overheads.c)
set_target_properties(stat_overheads PROPERTIES LINK_FLAGS " -DHAVE_ZHPE_STATS")
target_link_libraries(
  stat_overheads PUBLIC zhpe_stats zhpeq_util)
install(TARGETS stat_overheads DESTINATION libexec)

add_executable(stat_pthread_spinlock stat_pthread_spinlock.c)
set_target_properties(stat_pthread_spinlock PROPERTIES LINK_FLAGS " -DHAVE_ZHPE_STATS")
target_link_libraries(
  stat_pthread_spinlock PUBLIC zhpe_stats zhpeq_util)
install(TARGETS stat_pthread_spinlock DESTINATION libexec)
