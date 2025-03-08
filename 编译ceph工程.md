链接: http://strugglesquirrel.com/2019/01/03/手动编译ceph/  #来源:奋斗松鼠    
    
我的虚拟机内核版本:  
Linux localhost.localdomain 3.10.0-1160.el7.x86_64 #1 SMP Mon Oct 19 16:18:59 UTC 2020 x86_64 x86_64 x86_64 GNU/Linux  
编译过程中还是需要了一些问题，我相信链接文章里的编译过程也远没有像写的那样傻瓜式一站到底。  
首先执行命令：ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo" bash -xv do_cmake.sh  
遇到报错如下:  
```sh
[root@localhost ceph]# bash -xv do_cmake.sh
#!/usr/bin/env bash
set -ex
+ set -ex

git submodule update --init --recursive
+ git submodule update --init --recursive

: ${BUILD_DIR:=build}
+ : build
: ${CEPH_GIT_DIR:=..}
+ : ..

if [ -e $BUILD_DIR ]; then
    echo "'$BUILD_DIR' dir already exists; either rm -rf '$BUILD_DIR' and re-run, or set BUILD_DIR env var to a different directory name"
    exit 1
fi
+ '[' -e build ']'

PYBUILD="2"
+ PYBUILD=2
if [ -r /etc/os-release ]; then
  source /etc/os-release
  case "$ID" in
      fedora)
          PYBUILD="3.7"
          if [ "$VERSION_ID" -eq "32" ] ; then
              PYBUILD="3.8"
          elif [ "$VERSION_ID" -ge "33" ] ; then
              PYBUILD="3.9"
          fi
          ;;
      rhel|centos)
          MAJOR_VER=$(echo "$VERSION_ID" | sed -e 's/\..*$//')
          if [ "$MAJOR_VER" -ge "8" ] ; then
              PYBUILD="3.6"
          fi
          ;;
      opensuse*|suse|sles)
          PYBUILD="3"
          ARGS+=" -DWITH_RADOSGW_AMQP_ENDPOINT=OFF"
          ARGS+=" -DWITH_RADOSGW_KAFKA_ENDPOINT=OFF"
          ;;
  esac
elif [ "$(uname)" == FreeBSD ] ; then
  PYBUILD="3"
  ARGS+=" -DWITH_RADOSGW_AMQP_ENDPOINT=OFF"
  ARGS+=" -DWITH_RADOSGW_KAFKA_ENDPOINT=OFF"
else
  echo Unknown release
  exit 1
fi
+ '[' -r /etc/os-release ']'
+ source /etc/os-release
NAME="CentOS Linux"
++ NAME='CentOS Linux'
VERSION="7 (Core)"
++ VERSION='7 (Core)'
ID="centos"
++ ID=centos
ID_LIKE="rhel fedora"
++ ID_LIKE='rhel fedora'
VERSION_ID="7"
++ VERSION_ID=7
PRETTY_NAME="CentOS Linux 7 (Core)"
++ PRETTY_NAME='CentOS Linux 7 (Core)'
ANSI_COLOR="0;31"
++ ANSI_COLOR='0;31'
CPE_NAME="cpe:/o:centos:centos:7"
++ CPE_NAME=cpe:/o:centos:centos:7
HOME_URL="https://www.centos.org/"
++ HOME_URL=https://www.centos.org/
BUG_REPORT_URL="https://bugs.centos.org/"
++ BUG_REPORT_URL=https://bugs.centos.org/

CENTOS_MANTISBT_PROJECT="CentOS-7"
++ CENTOS_MANTISBT_PROJECT=CentOS-7
CENTOS_MANTISBT_PROJECT_VERSION="7"
++ CENTOS_MANTISBT_PROJECT_VERSION=7
REDHAT_SUPPORT_PRODUCT="centos"
++ REDHAT_SUPPORT_PRODUCT=centos
REDHAT_SUPPORT_PRODUCT_VERSION="7"
++ REDHAT_SUPPORT_PRODUCT_VERSION=7

+ case "$ID" in
++ sed -e 's/\..*$//'
++ echo 7
+ MAJOR_VER=7
+ '[' 7 -ge 8 ']'

if [[ "$PYBUILD" =~ ^3(\..*)?$ ]] ; then
    ARGS+=" -DWITH_PYTHON3=${PYBUILD}"
fi
+ [[ 2 =~ ^3(\..*)?$ ]]

if type ccache > /dev/null 2>&1 ; then
    echo "enabling ccache"
    ARGS+=" -DWITH_CCACHE=ON"
fi
+ type ccache

mkdir $BUILD_DIR
+ mkdir build
cd $BUILD_DIR
+ cd build
if type cmake3 > /dev/null 2>&1 ; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi
+ type cmake3
+ CMAKE=cmake
${CMAKE} $ARGS "$@" $CEPH_GIT_DIR || exit 1
+ cmake ..
-- The CXX compiler identification is GNU 4.8.5
-- The C compiler identification is GNU 4.8.5
-- The ASM compiler identification is GNU
-- Found assembler: /usr/bin/cc
-- Check for working CXX compiler: /usr/bin/c++
-- Check for working CXX compiler: /usr/bin/c++ -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Looking for pthread.h
-- Looking for pthread.h - found
-- Looking for pthread_create
-- Looking for pthread_create - not found
-- Looking for pthread_create in pthreads
-- Looking for pthread_create in pthreads - not found
-- Looking for pthread_create in pthread
-- Looking for pthread_create in pthread - found
-- Found Threads: TRUE
CMake Error at CMakeLists.txt:65 (message):
  Can't find sphinx-build.


-- Configuring incomplete, errors occurred!
See also "/SOURCECODE/ceph/build/CMakeFiles/CMakeOutput.log".
See also "/SOURCECODE/ceph/build/CMakeFiles/CMakeError.log".
+ exit 1
[root@localhost ceph]# yum install -y python-sphinx
已加载插件：fastestmirror, langpacks
Loading mirror speeds from cached hostfile
There are no enabled repos.
 Run "yum repolist all" to see the repos you have.
 To enable Red Hat Subscription Management repositories:
     subscription-manager repos --enable <repo>
 To enable custom repositories:
     yum-config-manager --enable <repo>

```
需要安装python插件  
```sh
 pip3 install Sphinx
```  
再执行报错如下：  
```sh
[root@localhost ceph]# ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo" bash -xv do_cmake.sh
#!/usr/bin/env bash
set -ex
+ set -ex

git submodule update --init --recursive
+ git submodule update --init --recursive

: ${BUILD_DIR:=build}
+ : build
: ${CEPH_GIT_DIR:=..}
+ : ..

if [ -e $BUILD_DIR ]; then
    echo "'$BUILD_DIR' dir already exists; either rm -rf '$BUILD_DIR' and re-run, or set BUILD_DIR env var to a different directory name"
    exit 1
fi
+ '[' -e build ']'

PYBUILD="2"
+ PYBUILD=2
if [ -r /etc/os-release ]; then
  source /etc/os-release
  case "$ID" in
      fedora)
          PYBUILD="3.7"
          if [ "$VERSION_ID" -eq "32" ] ; then
              PYBUILD="3.8"
          elif [ "$VERSION_ID" -ge "33" ] ; then
              PYBUILD="3.9"
          fi
          ;;
      rhel|centos)
          MAJOR_VER=$(echo "$VERSION_ID" | sed -e 's/\..*$//')
          if [ "$MAJOR_VER" -ge "8" ] ; then
              PYBUILD="3.6"
          fi
          ;;
      opensuse*|suse|sles)
          PYBUILD="3"
          ARGS+=" -DWITH_RADOSGW_AMQP_ENDPOINT=OFF"
          ARGS+=" -DWITH_RADOSGW_KAFKA_ENDPOINT=OFF"
          ;;
  esac
elif [ "$(uname)" == FreeBSD ] ; then
  PYBUILD="3"
  ARGS+=" -DWITH_RADOSGW_AMQP_ENDPOINT=OFF"
  ARGS+=" -DWITH_RADOSGW_KAFKA_ENDPOINT=OFF"
else
  echo Unknown release
  exit 1
fi
+ '[' -r /etc/os-release ']'
+ source /etc/os-release
NAME="CentOS Linux"
++ NAME='CentOS Linux'
VERSION="7 (Core)"
++ VERSION='7 (Core)'
ID="centos"
++ ID=centos
ID_LIKE="rhel fedora"
++ ID_LIKE='rhel fedora'
VERSION_ID="7"
++ VERSION_ID=7
PRETTY_NAME="CentOS Linux 7 (Core)"
++ PRETTY_NAME='CentOS Linux 7 (Core)'
ANSI_COLOR="0;31"
++ ANSI_COLOR='0;31'
CPE_NAME="cpe:/o:centos:centos:7"
++ CPE_NAME=cpe:/o:centos:centos:7
HOME_URL="https://www.centos.org/"
++ HOME_URL=https://www.centos.org/
BUG_REPORT_URL="https://bugs.centos.org/"
++ BUG_REPORT_URL=https://bugs.centos.org/

CENTOS_MANTISBT_PROJECT="CentOS-7"
++ CENTOS_MANTISBT_PROJECT=CentOS-7
CENTOS_MANTISBT_PROJECT_VERSION="7"
++ CENTOS_MANTISBT_PROJECT_VERSION=7
REDHAT_SUPPORT_PRODUCT="centos"
++ REDHAT_SUPPORT_PRODUCT=centos
REDHAT_SUPPORT_PRODUCT_VERSION="7"
++ REDHAT_SUPPORT_PRODUCT_VERSION=7

+ case "$ID" in
++ sed -e 's/\..*$//'
++ echo 7
+ MAJOR_VER=7
+ '[' 7 -ge 8 ']'

if [[ "$PYBUILD" =~ ^3(\..*)?$ ]] ; then
    ARGS+=" -DWITH_PYTHON3=${PYBUILD}"
fi
+ [[ 2 =~ ^3(\..*)?$ ]]

if type ccache > /dev/null 2>&1 ; then
    echo "enabling ccache"
    ARGS+=" -DWITH_CCACHE=ON"
fi
+ type ccache

mkdir $BUILD_DIR
+ mkdir build
cd $BUILD_DIR
+ cd build
if type cmake3 > /dev/null 2>&1 ; then
    CMAKE=cmake3
else
    CMAKE=cmake
fi
+ type cmake3
+ CMAKE=cmake
${CMAKE} $ARGS "$@" $CEPH_GIT_DIR || exit 1
+ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
-- The CXX compiler identification is GNU 4.8.5
-- The C compiler identification is GNU 4.8.5
-- The ASM compiler identification is GNU
-- Found assembler: /usr/bin/cc
-- Check for working CXX compiler: /usr/bin/c++
-- Check for working CXX compiler: /usr/bin/c++ -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Looking for pthread.h
-- Looking for pthread.h - found
-- Looking for pthread_create
-- Looking for pthread_create - not found
-- Looking for pthread_create in pthreads
-- Looking for pthread_create in pthreads - not found
-- Looking for pthread_create in pthread
-- Looking for pthread_create in pthread - found
-- Found Threads: TRUE
CMake Error at cmake/modules/CephChecks.cmake:3 (message):
  GCC 7+ required due to C++17 requirements
Call Stack (most recent call first):
  CMakeLists.txt:90 (include)


-- Configuring incomplete, errors occurred!
See also "/SOURCECODE/ceph/build/CMakeFiles/CMakeOutput.log".
See also "/SOURCECODE/ceph/build/CMakeFiles/CMakeError.log".
+ exit 1

```  
    
```sh
报错文件中提示如下：
Determining if the pthread_create exist failed with the following output:
Change Dir: /SOURCECODE/ceph/build/CMakeFiles/CMakeTmp

Run Build Command(s):/usr/bin/gmake cmTC_bfbc4/fast
/usr/bin/gmake -f CMakeFiles/cmTC_bfbc4.dir/build.make CMakeFiles/cmTC_bfbc4.dir/build
gmake[1]: 进入目录“/SOURCECODE/ceph/build/CMakeFiles/CMakeTmp”
Building C object CMakeFiles/cmTC_bfbc4.dir/CheckSymbolExists.c.o
/usr/bin/cc    -o CMakeFiles/cmTC_bfbc4.dir/CheckSymbolExists.c.o   -c /SOURCECODE/ceph/build/CMakeFiles/CMakeTmp/CheckSymbolExists.c
Linking C executable cmTC_bfbc4
/usr/local/cmake/bin/cmake -E cmake_link_script CMakeFiles/cmTC_bfbc4.dir/link.txt --verbose=1
/usr/bin/cc      CMakeFiles/cmTC_bfbc4.dir/CheckSymbolExists.c.o  -o cmTC_bfbc4
CMakeFiles/cmTC_bfbc4.dir/CheckSymbolExists.c.o：在函数‘main’中：
CheckSymbolExists.c:(.text+0x16)：对‘pthread_create’未定义的引用
collect2: 错误：ld 返回 1
gmake[1]: *** [cmTC_bfbc4] 错误 1
gmake[1]: 离开目录“/SOURCECODE/ceph/build/CMakeFiles/CMakeTmp”
gmake: *** [cmTC_bfbc4/fast] 错误 2

File /SOURCECODE/ceph/build/CMakeFiles/CMakeTmp/CheckSymbolExists.c:
/* */
#include <pthread.h>

int main(int argc, char** argv)
{
  (void)argv;
#ifndef pthread_create
  return ((int*)(&pthread_create))[argc];
#else
  (void)argc;
  return 0;
#endif
}

```
看上去貌似是pthread_create so库找不到，再编译如下：  
```sh
-- The CXX compiler identification is GNU 4.8.5
-- The C compiler identification is GNU 4.8.5
-- The ASM compiler identification is GNU
-- Found assembler: /usr/bin/cc
-- Check for working CXX compiler: /usr/bin/c++
-- Check for working CXX compiler: /usr/bin/c++ -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Looking for pthread.h
-- Looking for pthread.h - found
-- Looking for pthread_create
-- Looking for pthread_create - not found
-- Looking for pthread_create in pthreads
-- Looking for pthread_create in pthreads - not found
-- Looking for pthread_create in pthread
-- Looking for pthread_create in pthread - found
-- Found Threads: TRUE
CMake Error at cmake/modules/CephChecks.cmake:3 (message):
  GCC 7+ required due to C++17 requirements
Call Stack (most recent call first):
  CMakeLists.txt:90 (include)


-- Configuring incomplete, errors occurred!
See also "/SOURCECODE/ceph/build/CMakeFiles/CMakeOutput.log".
See also "/SOURCECODE/ceph/build/CMakeFiles/CMakeError.log".
+ exit 1

```  
似乎是gcc编译器的版本过低导致库的链接出现问题：https://blog.csdn.net/younger_china/article/details/104885092    
接下去要升级gcc编译器  

  





  





