#!/usr/bin/env python3

import sys
import re


source_file = open("testfile.cpp", 'r')
lines = source_file.readlines()


from collections import namedtuple
# first we need to be sure that codegen tags match
# if codegen tags are not matching, then we're in trouble

currentTag = ''
lastClosingLine = 0
lastOpeningLine = 0

# ChangeSet = namedtuple('ChangeSet', 'lookup, line_start_delta, line_end_delta', defaults = 0)

# currentChangeSet = ChangeSet("",0,0)

for lNr, line in enumerate(lines):
	if "Codegen" in line:
		# we need to check whether we have an open tag or a closing tag
		tag = re.sub('^// Codegen <(.*?)>.*$', r'\1' , line).strip()

		if (tag != ''):
			# there is a valid tag, it could be an opening or a closing tag.
			if tag[0] != '/' and currentTag == '':
				currentTag = tag
				print ("Opening " + currentTag + " in line %s, delta lines=%s" %(lNr, lNr - lastClosingLine))
				lastOpeningLine = lNr
				# currentChangeSet.lookup = currentTag
				# currentChangeSet.line_start_delta = lNr - lastClosingLine
				
			elif tag[0] == '/' and currentTag != '' and tag[1:] == currentTag:
				lDelta = lNr - lastClosingLine
				print ("Closing " + currentTag + " in line %s, number of lines=%s" %(lNr, lNr - lastOpeningLine))
				currentTag = ''
				lastClosingLine = lNr
			else:
				print ("error: closing tag before opening tag")

