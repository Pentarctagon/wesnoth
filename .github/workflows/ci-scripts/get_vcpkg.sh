#!/bin/bash

if [ ! -d /Users/runner/vcpkg ]; then
	git clone https://github.com/microsoft/vcpkg.git /Users/runner/vcpkg
	cd /Users/runner/vcpkg
	git checkout 7e7dad5fe20cdc085731343e0e197a7ae655555b
	/Users/runner/vcpkg/bootstrap-vcpkg.sh
fi
