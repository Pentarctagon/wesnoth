#!/bin/bash

HOMEBREW_NO_AUTO_UPDATE=1 brew install automake ccache scons

# github actions+vcpkg has a bug where it prefers the mono include path over vcpkg's, so uninstall it
# see https://github.com/microsoft/vcpkg/issues/20367
#sudo rm -rf /Library/Frameworks/Mono.framework
#sudo pkgutil --forget com.xamarin.mono-MDK.pkg
#sudo rm /etc/paths.d/mono-commands

cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_GAME=true -DENABLE_SERVER=true -DENABLE_CAMPAIGN_SERVER=true -DENABLE_TESTS=false -DENABLE_MYSQL=false -DENABLE_NLS=false \
      -DVCPKG_TARGET_TRIPLET=x64-osx -DCMAKE_TOOLCHAIN_FILE=/Users/runner/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -G "Xcode" .
