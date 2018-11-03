#!/usr/bin/env python3

import sys
import re
from copy import copy # so that we can copy objects

from test_pycparser import EnumVisitor, ast

source_file = open("testfile.cpp", 'r')
lines = source_file.readlines()
source_file.close()

# first we need to be sure that codegen tags match
# if codegen tags are not matching, then we're in trouble

currentTag = ''
lastClosingLine = 0
lastOpeningLine = 0


class ChangeSet:
	name = ''
	startOffset = 0
	numLines = 0
	def __init__(self, name='', attr='', startOffset=0,numLines=0):
		self.name = name
		self.attr = attr
		self.startOffset = startOffset
		self.numLines = numLines
		

currentChangeSet = ChangeSet()
changeSets = []

for lNr, line in enumerate(lines):
	if "Codegen" in line:
		# we need to check whether we have an open tag or a closing tag
		matches = re.findall(r'^// Codegen <(.+?)(?:,\s*?([a-z,A-Z,0-9_]+?))?>.*$' , line)
		
		tag = ''     # name for the enum to look up via codegen, e.g. VkFormat
		tagAttr = '' # attribute for codegen, most probably type information, e.g. uint32_t

		if len(matches) > 0:
			print (matches)
			tag = (matches[0][0]).strip()
			tagAttr = matches[0][1].strip()

		print (tag)

		if (tag != ''):
			# there is a valid tag, it could be an opening or a closing tag.
			if tag[0] != '/' and currentTag == '':
				# open a new tag
				currentTag = tag
				lDelta = lNr - lastClosingLine 
				# print ("Opening " + currentTag + " in line %s, delta lines=%s" %(lNr, lDelta))
				lastOpeningLine = lNr 

				currentChangeSet.name = currentTag
				currentChangeSet.attr = tagAttr
				currentChangeSet.startOffset = lDelta

			elif tag[0] == '/' and currentTag != '' and tag[1:] == currentTag:
				# close the current tag
				lDelta = lNr - lastOpeningLine 
				# print ("Closing " + currentTag + " in line %s, number of lines=%s" %(lNr,lDelta))
				currentTag = ''
				lastClosingLine = lNr 

				currentChangeSet.numLines = lDelta
				# now copy the changeSet to the list of changeSets
				changeSets.append(copy(currentChangeSet))
			else:
				if (currentTag != '') :
					print ("error: tag mismatch: tag <%s> is probably not being closed, mismatch with <%s>" % (currentTag, tag))
				else :
					print ("error: tag mismatch: tag <%s> is probably not being opened." % tag)
				exit(1)



lastLine = 0
for i in changeSets:
	
	start = lastLine + i.startOffset +1 
	
	numLinesToRemove = i.numLines -1

	# print (start, numLinesToRemove)

	v = EnumVisitor(i.name, i.attr)
	generated_code = v.visit(ast).splitlines(keepends=1)
	#print (generated_code)
	# generated_code = ""

	del lines[ start : start + numLinesToRemove ]

	linesInserted = 0 # note how many lines were added
	
	for i, source in enumerate(generated_code):
		lines.insert( start + i, source)
		linesInserted += 1

	lastLine = start + linesInserted

for lnr, l in enumerate(lines):
 	print ('{0: <5}'.format(lnr), l, end='')


