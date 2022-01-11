#!/bin/bash

# TODO: set macos min version for vcpkg+wesnoth

HOMEBREW_NO_AUTO_UPDATE=1 brew install automake ccache scons

export PATH="/usr/local/opt/ccache/libexec:/usr/local/opt/gettext/bin:$PWD/utils/CI:$PATH"
export CC=ccache-clang
export CXX=ccache-clang++
export CCACHE_MAXSIZE=3000M
export CCACHE_COMPILERCHECK=content
export CCACHE_DIR="/Users/runner/build-cache"

sudo rm -rf /Library/Frameworks/Mono.framework
sudo pkgutil --forget com.xamarin.mono-MDK.pkg
sudo rm /etc/paths.d/mono-commands

scons translations build=release --debug=time nls=true jobs=2 || exit 1

cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_GAME=true -DENABLE_SERVER=true -DENABLE_CAMPAIGN_SERVER=true -DENABLE_TESTS=false -DENABLE_MYSQL=false -DENABLE_NLS=false -DVCPKG_TARGET_TRIPLET=x64-osx -DCMAKE_TOOLCHAIN_FILE=/Users/runner/vcpkg/scripts/buildsystems/vcpkg.cmake -G "Xcode" .

cmake --build . --target "wesnoth" --config "Release" -- CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO
EXIT_VAL=$?

ccache -s
ccache -z

exit $EXIT_VAL
