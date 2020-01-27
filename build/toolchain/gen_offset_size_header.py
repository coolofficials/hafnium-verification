#!/usr/bin/env python3
#
# Copyright 2019 The Hafnium Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Generate a header file with definitions of constants parsed from a binary."""

import argparse
import os
import re
import subprocess
import sys

HF_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
BINUTILS_ROOT = os.path.join(HF_ROOT, "prebuilts", "linux-x64", "gcc", "bin")
STRINGS = os.path.join(BINUTILS_ROOT, "aarch64-linux-android-strings")

PROLOGUE = """
/**
 * This file was auto-generated by {}.
 * Changes will be overwritten.
 */

#pragma once

""".format(__file__)

def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("bin_file",
		help="binary file to be parsed for definitions of constants")
	parser.add_argument("out_file", help="output file");
	args = parser.parse_args()

	# Regex for finding definitions: <HAFNIUM_DEFINE name #value />
	regex = re.compile(r'<HAFNIUM_DEFINE\s([A-Za-z0-9_]+)\s#([0-9]+) />')

	# Extract strings from the input binary file.
	stdout = subprocess.check_output([ STRINGS, args.bin_file ])
	stdout = str(stdout).split(os.linesep)

	with open(args.out_file, "w") as f:
		f.write(PROLOGUE)
		for line in stdout:
			for match in regex.findall(line):
				f.write("#define {} ({})\n".format(
					match[0], match[1]))

if __name__ == "__main__":
    sys.exit(main())