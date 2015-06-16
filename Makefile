#COMAKE2 edit-mode: -*- Makefile -*-
####################64Bit Mode####################
ifeq ($(shell uname -m),x86_64)
CC=gcc
CXX=g++
CXXFLAGS=-g \
  -pipe \
  -W \
  -Wall \
  -fPIC
CFLAGS=-g \
  -pipe \
  -W \
  -Wall \
  -fPIC
CPPFLAGS=-D_GNU_SOURCE \
  -D__STDC_LIMIT_MACROS \
  -DVERSION=\"1.9.8.7\"
INCPATH=-I. \
  -I./include \
  -I./output \
  -I./output/include
DEP_INCPATH=-I../third-64/zookeeper \
  -I../third-64/zookeeper/include \
  -I../third-64/zookeeper/output \
  -I../third-64/zookeeper/output/include

#============ CCP vars ============
CCHECK=@ccheck.py
CCHECK_FLAGS=
PCLINT=@pclint
PCLINT_FLAGS=
CCP=@ccp.py
CCP_FLAGS=


#COMAKE UUID
COMAKE_MD5=9e0d243d91d01ae64781b2695859b06e  COMAKE


.PHONY:all
all:comake2_makefile_check test leader_follower 
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mall[0m']"
	@echo "make all done"

.PHONY:comake2_makefile_check
comake2_makefile_check:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mcomake2_makefile_check[0m']"
	#in case of error, update 'Makefile' by 'comake2'
	@echo "$(COMAKE_MD5)">comake2.md5
	@md5sum -c --status comake2.md5
	@rm -f comake2.md5

.PHONY:ccpclean
ccpclean:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mccpclean[0m']"
	@echo "make ccpclean done"

.PHONY:clean
clean:ccpclean
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mclean[0m']"
	rm -rf test
	rm -rf ./output/bin/test
	rm -rf leader_follower
	rm -rf ./output/bin/leader_follower
	rm -rf test_test.o
	rm -rf test_zkclient.o
	rm -rf leader_follower_leader_follower.o
	rm -rf leader_follower_zkclient.o

.PHONY:dist
dist:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mdist[0m']"
	tar czvf output.tar.gz output
	@echo "make dist done"

.PHONY:distclean
distclean:clean
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mdistclean[0m']"
	rm -f output.tar.gz
	@echo "make distclean done"

.PHONY:love
love:
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mlove[0m']"
	@echo "make love done"

test:test_test.o \
  test_zkclient.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mtest[0m']"
	$(CXX) test_test.o \
  test_zkclient.o -Xlinker "-("  ../third-64/zookeeper/lib/libzookeeper_mt.a \
  ../third-64/zookeeper/lib/libzookeeper_st.a -lpthread \
  -lcrypto \
  -lrt -Xlinker "-)" -o test
	mkdir -p ./output/bin
	cp -f --link test ./output/bin

leader_follower:leader_follower_leader_follower.o \
  leader_follower_zkclient.o
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mleader_follower[0m']"
	$(CXX) leader_follower_leader_follower.o \
  leader_follower_zkclient.o -Xlinker "-("  ../third-64/zookeeper/lib/libzookeeper_mt.a \
  ../third-64/zookeeper/lib/libzookeeper_st.a -lpthread \
  -lcrypto \
  -lrt -Xlinker "-)" -o leader_follower
	mkdir -p ./output/bin
	cp -f --link leader_follower ./output/bin

test_test.o:test.cc \
  zkclient.h
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mtest_test.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o test_test.o test.cc

test_zkclient.o:zkclient.cc \
  zkclient.h
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mtest_zkclient.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o test_zkclient.o zkclient.cc

leader_follower_leader_follower.o:leader_follower.cc \
  zkclient.h
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mleader_follower_leader_follower.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o leader_follower_leader_follower.o leader_follower.cc

leader_follower_zkclient.o:zkclient.cc \
  zkclient.h
	@echo "[[1;32;40mCOMAKE:BUILD[0m][Target:'[1;32;40mleader_follower_zkclient.o[0m']"
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o leader_follower_zkclient.o zkclient.cc

endif #ifeq ($(shell uname -m),x86_64)


