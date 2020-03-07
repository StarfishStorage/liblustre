#!/bin/bash
# don't forget to exclude clean.sh from list

make clean distclean

declare -a FLIST
FLIST[0]="Makefile Makefile.in aclocal.m4 autom4te.cache/ config.guess config.sub configure"
FLIST[1]="depcomp include/Makefile include/Makefile.in include/config.h include/config.h.in include/stamp-h1 install-sh lib/.deps/"
FLIST[2]="lib/.libs/ lib/Makefile lib/Makefile.in lib/liblustre.la lib/liblustre.pc lib/liblustre_la-file.lo"
FLIST[3]="lib/liblustre_la-liblustre.lo lib/liblustre_la-liblustreapi_hsm.lo lib/liblustre_la-liblustreapi_layout.lo"
FLIST[4]="lib/liblustre_la-logging.lo lib/liblustre_la-misc.lo lib/liblustre_la-osts.lo lib/liblustre_la-params.lo"
FLIST[5]="lib/liblustre_la-strings.lo liblustre-0.2.0.tar.xz liblustre.spec libtool ltmain.sh"
FLIST[6]="m4/libtool.m4 m4/ltoptions.m4 m4/ltsugar.m4 m4/ltversion.m4 m4/lt~obsolete.m4 man/Makefile man/Makefile.in"
FLIST[7]="man/liblustre.7 man/lus_create_volatile_by_fid.3 man/lus_hsm_action_begin.3 man/lus_hsm_copytool_register.3"
FLIST[8]="man/lus_mdt_stat_by_fid.3 man/lus_open_fs.3 man/lus_stat_by_fid.3 missing rpms/"
FLIST[9]="tests/.deps/ tests/.libs/ tests/Makefile tests/Makefile.in tests/group_lock_test"
FLIST[10]="tests/group_lock_test-group_lock_test.o tests/lib_test tests/lib_test-lib_test.o tests/liblustre_support.la"
FLIST[11]="tests/liblustre_support_la-support.lo tests/liblustre_unittest.la tests/liblustre_unittest_la-liblustre.lo"
FLIST[12]="tests/liblustre_unittest_la-liblustreapi_hsm.lo tests/liblustre_unittest_la-liblustreapi_layout.lo"
FLIST[13]="tests/liblustre_unittest_la-logging.lo tests/liblustre_unittest_la-strings.lo tests/liblustre_unittest_la-test_file.lo"
FLIST[14]="tests/liblustre_unittest_la-test_misc.lo tests/liblustre_unittest_la-test_osts.lo"
FLIST[15]="tests/liblustre_unittest_la-test_params.lo tests/liblustre_unittest_la-test_support.lo tests/llapi_fid_test"
FLIST[16]="tests/llapi_fid_test-llapi_fid_test.o tests/llapi_hsm_test tests/llapi_hsm_test-llapi_hsm_test.o tests/llapi_layout_test"
FLIST[17]="tests/llapi_layout_test-llapi_layout_test.o tests/posixct tests/posixct-posixct.o tests/posixct-strings.o"

for N in $(seq 0 17); do 
   rm -rf ${FLIST[$N]}
done

# then do 
# autoreconf -i
# ./configure
# make (copy lib/.libs)
# make rpms
