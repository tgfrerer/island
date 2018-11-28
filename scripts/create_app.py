#!/usr/bin/python3

from argparse import ArgumentParser
from os import path
from os import makedirs
from os import rename
from os import system
from os import listdir
import os
import shutil
from shutil import copystat
import stat
import errno

class Error(EnvironmentError):
    pass

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

def copy_tree(src, dst, template_name, app_name, symlinks=False, ignore=None, copy_function=shutil.copy2,
			 ignore_dangling_symlinks=False):
	
	names = os.listdir(src)

	if ignore is not None:
		ignored_names = ignore(src, names)
	else:
		ignored_names = set()

	os.makedirs(dst)

	errors = []
	for name in names:
		if name in ignored_names:
			continue
		print("name: %s" % name)
		srcname = os.path.join(src, name)
		dstname = os.path.join(dst, name.replace(template_name, app_name, 1))
		print ("dstname: %s" % dstname)
		try:
			if os.path.islink(srcname):
				linkto = os.readlink(srcname)
				if symlinks:
					# We can't just leave it to `copy_function` because legacy
					# code with a custom `copy_function` may rely on copytree
					# doing the right thing.
					os.symlink(linkto, dstname)
					shutil.copystat(srcname, dstname, follow_symlinks=not symlinks)
				else:
					# ignore dangling symlink if the flag is on
					if not os.path.exists(linkto) and ignore_dangling_symlinks:
						continue
					# otherwise let the copy occur. copy2 will raise an error
					copy_function(srcname, dstname)
			elif os.path.isdir(srcname):
				copy_tree(srcname, dstname, template_name, app_name, symlinks, ignore, copy_function)
			else:
				# Will raise a SpecialFileError for unsupported file types
				# copy file
				copy_function(srcname, dstname)
		# catch the Error from the recursive copytree so that we can
		# continue with other files
		except Error as err:
			errors.extend(err.args[0])
		except EnvironmentError as why:
			errors.append((srcname, dstname, str(why)))
	try:
		copystat(src, dst)
	except OSError as why:
		if WindowsError is not None and isinstance(why, WindowsError):
			# Copying file access times may fail on Windows
			pass
		else:
			errors.append((src, dst, str(why)))
	if errors:
		raise Error(errors)
	return dst


app_name                            = args.app_name
app_module_name                     = app_name + "_app"
template_name                       = args.template_name
app_name_camelcase_capitalised      = to_titled_camel_case(app_name)
template_name_camelcase_capitalised = to_titled_camel_case(template_name)
app_dir                             = ("./%s" % app_name)
app_module_dir                      = ("%s/%s" % (app_dir, app_module_name))
script_dir                          = path.split(path.realpath(__file__))[0] # directory where this script lives
template_source_dir                 = ("%s/templates/app/%s" % (script_dir, template_name))


print("App name: %s" % app_name)
print("App module directory: %s" % app_module_dir)
print("Template source directory: %s" % template_source_dir)

# Go through all files in target directory recursively,
# and apply replacements to their contents
def process_directory_recursively(dir_path, replacements):
	for item in replacements:
		system("find -P %s -type f -exec sed -i 's/%s/g' {} +" % (dir_path, item))

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

	# Create a copy of the template in our target directory.
	# While copying template_name is substituted for app_name
	copy_tree(template_source_dir, app_dir, template_name, app_name, ignore=shutil.ignore_patterns('CMakeLists.txt.user', 'build'))
	
	# Replace template names with app names in all target files
	replacements = {'%s/%s' % (template_name, app_name), 
					'%s/%s' % (template_name_camelcase_capitalised, app_name_camelcase_capitalised)}
	process_directory_recursively(app_dir, replacements)

	print ("Done.")