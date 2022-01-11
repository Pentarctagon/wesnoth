#!/bin/bash

export PATH="/usr/local/opt/ccache/libexec:/usr/local/opt/gettext/bin:/Users/runner/work/wesnoth/wesnoth/utils/CI:$PATH"
export CCACHE_MAXSIZE=3000M
export CCACHE_COMPILERCHECK=content
export CCACHE_DIR="/Users/runner/build-cache"
export CC=ccache-clang
export CXX=ccache-clang++

scons translations build=release --debug=time nls=true jobs=2 || exit 1

cmake --build . --target "wesnoth" --config "Release" -- CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED="NO" CODE_SIGN_ENTITLEMENTS="" CODE_SIGNING_ALLOWED="NO"

/Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/MacOS/wesnoth --simple-version
/Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/MacOS/wesnoth --report

ccache -s
ccache -z
