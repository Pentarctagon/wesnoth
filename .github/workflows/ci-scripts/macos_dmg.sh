#!/bin/bash

mkdir /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents
mkdir /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources
cp -r /Users/runner/work/wesnoth/wesnoth/data /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/data
cp -r /Users/runner/work/wesnoth/wesnoth/fonts /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/fonts
cp -r /Users/runner/work/wesnoth/wesnoth/images /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/images
cp -r /Users/runner/work/wesnoth/wesnoth/sounds /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/sounds
cp -r /Users/runner/work/wesnoth/wesnoth/translations /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/translations
cp -r /Users/runner/work/wesnoth/wesnoth/projectfiles/Xcode/Resources/SDLMain.nib /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/SDLMain.nib
cp /Users/runner/work/wesnoth/wesnoth/projectfiles/Xcode/Resources/container-migration.plist /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/container-migration.plist
cp /Users/runner/work/wesnoth/wesnoth/projectfiles/Xcode/Resources/icon.icns /Users/runner/work/wesnoth/wesnoth/Release/wesnoth.app/Contents/Resources/icon.icns

hdiutil create -volname "Wesnoth_Release" -fs 'HFS+' -srcfolder "Release" -ov -format UDBZ "Wesnoth_Release.dmg"
