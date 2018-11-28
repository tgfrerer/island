#!/usr/bin/python

from argparse import ArgumentParser
from os import path
from os import makedirs
from os import rename
from os import system
import shutil

#Arguments 

parser = ArgumentParser( description='Create a new Island application based on application template')
parser.add_argument('-a', '--app', dest='app_name', required=True, help="Specify the name for new application to create from template.")
parser.add_argument("-t","--template_name", dest='template_name', help="Specify the name for template.", default="triangle")


args = parser.parse_args()

def to_camel_case(snake_str):
	components = snake_str.split('_')
	# We capitalize the first letter of each component except the first one
	# with the 'title' method and join them together.
	return components[0] + ''.join(x.title() for x in components[1:])

def to_titled_camel_case(snake_str):
	components = snake_str.split('_')
	# We capitalize the first letter of each component.
	return ''.join(x.title() for x in components)


def copyTree(src, dest):
### Copies a directory tree, ignores CMakeLists.txt.user files.
	try:
		shutil.copytree(src, dest, ignore=shutil.ignore_patterns('CMakeLists.txt.user'))
	except OSError as e:
		# If the error was caused because the source wasn't a directory
		if e.errno == errno.ENOTDIR:
			shutil.copy(src, dest)
		else:
			print('Directory not copied. Error: %s' % e)

app_name = args.app_name
app_module_name = app_name + "_app"
template_name = args.template_name
app_name_camelcase_capitalised = to_titled_camel_case(app_name)
template_name_camelcase_capitalised = to_titled_camel_case(template_name)
app_dir = ("./%s" % app_name)
app_module_dir = ("%s/%s" % (app_dir, app_module_name))
script_dir = path.split(path.realpath(__file__))[0] # directory where this script lives
template_source_dir = ("%s/templates/app/%s" % (script_dir, template_name))


print("App name: %s" % app_name)
print("App module directory: %s" % app_module_dir)
print("Template source directory: %s" % template_source_dir)

# Go through all files in target directory recursively,
# and apply replacements to their contents
def process_directory_recursively(dir_path, replacements):
	for item in replacements:
		system("find -P %s -type f -exec sed -i 's/%s/g' {} +" % (dir_path, item))

# Go through all files in target directory recursively,
# and apply replacements to file and directory names.
def rename_target_files_recursively(dir_path, file_name_replacements):
	# first recursively rename directories
	for pair in file_name_replacements:
		exec_str = "find -P %s -name '*%s*' -type d -exec bash -c 'mv \"$0\" \"${0/%s/%s}\"' {} +" % (dir_path, pair[0], pair[0], pair[1] )
		# print (exec_str)
		system(exec_str)
	# then recursively rename files in directories
	for pair in file_name_replacements:
		exec_str = "find -P %s -name '*%s*' -type f -exec bash -c 'mv \"$0\" \"${0/%s/%s}\"' {} -- \;" % (dir_path, pair[0], pair[0], pair[1] )
		# print (exec_str)
		system(exec_str)


# check if directory with given module name exists
# if yes, exit with an error message
# if no, create directory and populate it by copying template 
# update template tags with module name 
if (path.isdir(app_dir)):
	print ("Target directory %s already exists, aborting mission." % app_dir)
	exit
	pass
else:
	print ("Creating Application `%s`" % app_name)
	# create directory for storing the module

	if not path.isdir(template_source_dir):
		print ("ERROR: Could not find template at: `%s`, aborting mission." % template_source_dir)
		exit(1)

	# ---------- invariant template source dir exists.

	# Create a copy of the template in our target directory
	copyTree(template_source_dir, app_dir)
	
	# Replace template filenames with our app filenames
	file_name_replacements = [ [template_name, app_name] ]
	rename_target_files_recursively(app_dir, file_name_replacements)

	# Replace template names with app names in all target files
	replacements = {'%s/%s' % (template_name, app_name), 
					'%s/%s' % (template_name_camelcase_capitalised, app_name_camelcase_capitalised)}
	process_directory_recursively(app_dir, replacements)

	print ("Done.")