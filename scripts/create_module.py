#!/usr/bin/python

from argparse import ArgumentParser
from os import path
from os import makedirs
from os import rename

#Arguments 

parser = ArgumentParser(description='Create a new Island module based on module template')
parser.add_argument( dest="module_name", metavar='MODULE_NAME', type=str, 
	nargs=1, help="specify the name for given module")

args = parser.parse_args()



module_name = args.module_name[0]
module_dir = ("./%s" % module_name)
script_dir = path.split(path.realpath(__file__))[0] # directory where this script lives

print("Module name: %s" % module_name)
print("Module directory: %s" % module_dir)

# check if directory with given module name exists
# if yes, exit with an error message
# if no, create directory and populate it by copying template 
# update template tags with module name 

# Create a file based on template, scanning and replacing tokens in the process
def process_file(in_file_path, out_file_path, replacements):
	with open(in_file_path) as infile, open(out_file_path, 'w') as outfile:
	    for line in infile:
	        for src, target in replacements.iteritems():
	            line = line.replace(src, target)
	        outfile.write(line)


if (path.isdir(module_dir)):
	print ("Target directory %s already exists, aborting mission." % module_dir)
	exit
	pass
else:
	print ("Creating Module `%s`" % module_name)
	# create directory for storing the module
	makedirs(module_dir)
	source_dir = ("%s/templates/module/" %  script_dir)
	replacements = {'@module_name@': module_name}
	# process files from the template 
	process_file("%s/CMakeLists.txt.template" % source_dir    , "%s/CMakeLists.txt" % (module_dir             ), replacements)
	process_file("%s/implementation.cpp.template" % source_dir, "%s/%s.cpp"         % (module_dir, module_name), replacements)
	process_file("%s/interface.h.template" % source_dir       , "%s/%s.h"           % (module_dir, module_name), replacements)
