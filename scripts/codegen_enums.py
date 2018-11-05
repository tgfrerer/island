#!/usr/bin/env python3

import sys, re, datetime, argparse, os.path
from copy import copy # so that we can copy objects
import shutil

from vk_enums_generator import EnumVisitor, ast


def codegen_enums(inputFilePath, outputFilePath = ''):

	if False == os.path.isfile(inputFilePath):
		print("Input file `%s` not found, aborting mission" % inputFilePath)
		exit(1)

	source_file = open(inputFilePath, 'r')
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
		def __init__(self, name='', attr='', isCEnum = 0, startOffset=0,numLines=0):
			self.name = name
			self.attr = attr
			self.isCEnum = isCEnum
			self.startOffset = startOffset
			self.numLines = numLines
			

	currentChangeSet = ChangeSet()
	changeSets = []

	for lNr, line in enumerate(lines):
		if "Codegen" in line:
			# we need to check whether we have an open tag or a closing tag
			matches = re.findall(r'^// Codegen <(.+?)(?:,\s*?([a-z,A-Z,0-9_]+?)(?:,\s*?([a-z,A-Z,0-9_]+?))?)?>.*$' , line)
			
			tag = ''     # name for the enum to look up via codegen, e.g. VkFormat
			tagAttr = '' # attribute for codegen, most probably type information, e.g. uint32_t
			tagIsCEnum = 0 # whether a tag emits a c enum (0 == false == default)  

			if len(matches) > 0:
				# print (matches)
				tag = (matches[0][0]).strip()
				tagAttr = matches[0][1].strip()
				if matches[0][2].strip() == 'c':
					# By default we generate cpp-style class enums
					# If 'c' is explicitly specified, flag to create
					# c-style enum.
					tagIsCEnum = 1

				# print (matches)

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
					currentChangeSet.isCEnum = tagIsCEnum

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
						print ("ERROR: tag mismatch: tag <%s> is probably not being closed, mismatch with <%s>" % (currentTag, tag))
					else :
						print ("ERROR: tag mismatch: tag <%s> is probably not being opened." % tag)
					print("No code generated, aborting mission.")
					sys.exit(1)


	lastLine = 0
	for i in changeSets:
		
		start = lastLine + i.startOffset +1 
		
		numLinesToRemove = i.numLines -1

		# print (start, numLinesToRemove)

		v = EnumVisitor(i.name, i.attr, i.isCEnum)
		generated_code = v.visit(ast).splitlines(keepends=1)
		#print (generated_code)
		# generated_code = ""

		del lines[ start : start + numLinesToRemove ]

		linesInserted = 0 # note how many lines were added
		
		# Uncommenting the next two lines will add timecode comments to generated code.
		# lines.insert(start, "// ** generated %s ** \n" % datetime.datetime.utcnow().isoformat())
		# start += 1

		for i, source in enumerate(generated_code):
			lines.insert( start + i, source)
			linesInserted += 1

		lastLine = start + linesInserted

	# for lnr, l in enumerate(lines):
	#   print ('{0: <5}'.format(lnr), l, end='')

	if (outputFilePath == '' or outputFilePath == None):
		outputFile = sys.stdout
	else:
		if (os.path.isfile(outputFilePath)):
			# output file already exists - we better create a backup
			shutil.copy2(outputFilePath, "%s.backup" % outputFilePath )
			print("Created backup for `%s`." % outputFilePath)
		outputFile = open(outputFilePath,'w')

	for l in lines:
	    print(l, end='', file=outputFile)

	if outputFile != sys.stdout:
		print("Generated %s enums in output file: `%s`" % (len(changeSets), outputFile.name))

if __name__ == "__main__":
	parser = argparse.ArgumentParser( description='Generate code for enums tagged with `// Codegen <VkEnumName, [typename], [c]>`')
	parser.add_argument('-i', '--inputFile', dest='inFile', required=True)
	parser.add_argument("-o","--outputFile", dest='outFile', help="Output file name (will overwrite if exists!)")
	args = parser.parse_args()
	codegen_enums(args.inFile, args.outFile)
	
