#!/usr/bin/python3

from argparse import ArgumentParser
import os
from os import path
import shutil
from shutil import copystat

SCRIPTS_PATH = os.path.dirname(os.path.realpath(__file__))
CWD_PATH = os.path.realpath(os.getcwd())


class Error(EnvironmentError):
    pass

# Arguments


parser = ArgumentParser(
    description='Create a new Island project based on a template / or an existing project.')
parser.add_argument('project_name',
                    help='Specify the name for new project to create from template.')
parser.add_argument('-T', '--template-dir', dest='template_dir',
                    help='Specify a path *relative to the current directory* in which to look for project template directories. Use dot (".") to search for project directories within the current directory - for example if you wish to duplicate an existing project as a starting point for a new project.', default="{scripts_path}/../apps/templates".format(scripts_path=SCRIPTS_PATH))
parser.add_argument('-t', '--template-name', dest='template_name',
                    help='Specify the name for template. This can be the name of any project directory within TEMPLATE_DIR.', default='triangle')

args = parser.parse_args()


def copy_function_wrapper(src, dst, copy_function=None, replacements=None):
    # in case we have a text file, we want to do a line-by-line
    # search-and-replace for the template terms, otherwise we
    # just use the fastest method to copy the file across.
    if (os.path.splitext(src)[1] in [".cpp", ".h", ".txt"]):
        with open(src, 'r') as infile, open(dst, 'w') as outfile:
            for line in infile:
                for src, target in replacements.items():
                    line = line.replace(src, target)
                outfile.write(line)
    elif copy_function is not None:
        # if file is not a templated file, then we just copy it across
        copy_function(src, dst)


def copy_tree(src, dst, template_name, project_name, symlinks=False, ignore=None,
              copy_function=shutil.copy2,
              ignore_dangling_symlinks=False,
              replacements=None):

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
        # print("name: %s" % name)
        srcname = os.path.join(src, name)
        dstname = os.path.join(dst, name.replace(template_name, project_name, 1))
        # print ("dstname: %s" % dstname)
        try:
            if os.path.islink(srcname):
                linkto = os.readlink(srcname)
                if symlinks:
                    # We can't just leave it to `copy_function` because legacy
                    # code with a custom `copy_function` may rely on copytree
                    # doing the right thing.
                    os.symlink(linkto, dstname)
                    shutil.copystat(srcname, dstname,
                                    follow_symlinks=not symlinks)
                else:
                    # ignore dangling symlink if the flag is on
                    if not os.path.exists(linkto) and ignore_dangling_symlinks:
                        continue
                    # otherwise let the copy occur. copy2 will raise an error
                    copy_function_wrapper(srcname, dstname,
                                          copy_function=copy_function,
                                          replacements=replacements)
            elif os.path.isdir(srcname):
                copy_tree(srcname, dstname, template_name,
                          project_name, symlinks, ignore,
                          copy_function, replacements=replacements)
            else:
                # Will raise a SpecialFileError for unsupported file types
                # copy file
                copy_function_wrapper(srcname, dstname,
                                      copy_function=copy_function,
                                      replacements=replacements)
        # catch the Error from the recursive copytree so that we can
        # continue with other files
        except Error as err:
            errors.extend(err.args[0])
        except EnvironmentError as why:
            errors.append((srcname, dstname, str(why)))
    try:
        copystat(src, dst)
    except OSError as why:
        if OSError is not None and isinstance(why, OSError):
            # Copying file access times may fail on Windows
            pass
        else:
            errors.append((src, dst, str(why)))
    if errors:
        raise Error(errors)
    return dst


def to_camel_case(snake_str):
    components = snake_str.split('_')
    # We capitalize the first letter of each component except the first one
    # with the 'title' method and join them together.
    return components[0] + ''.join(x.title() for x in components[1:])


def to_titled_camel_case(snake_str):
    components = snake_str.split('_')
    # We capitalize the first letter of each component.
    return ''.join(x.title() for x in components)


project_name = args.project_name
app_module_name = project_name + '_app'

template_source_dir = args.template_dir

if (os.path.isabs(template_source_dir) is False):
    # we have a relative path given for template dir, we must prepend
    # the CWD_PATH to the relative path given, as all relative paths
    # are relative to where the script was executed
    template_source_dir = os.path.join(CWD_PATH, template_source_dir)

template_name = args.template_name.rstrip(os.path.sep) # remove any trailing path separators that might have come via autocomplete
project_name_camelcase_capitalised = to_titled_camel_case(project_name)
template_name_camelcase_capitalised = to_titled_camel_case(template_name)

app_dir = ('./%s' % project_name)
app_module_dir = os.path.normpath(os.path.join(app_dir, app_module_name))

template_source_dir = os.path.normpath(
        os.path.relpath(
            os.path.join(template_source_dir, template_name),
            CWD_PATH
        ))

print('App name: %s' % project_name)
print('App module directory: %s' % app_module_dir)
print('Template name: %s' % template_name)
print('Template source directory: %s' % template_source_dir)


# Go through all files in target directory recursively,
# and apply replacements to their contents


# check if directory with given module name exists
# if yes, exit with an error message
# if no, create directory and populate it by copying template
# update template tags with module name
if (path.isdir(app_dir)):
    print('Target directory %s already exists, aborting mission.' % app_dir)
    exit
    pass
else:
    print('Creating Application `%s`' % project_name)
    # create directory for storing the module

    if not path.isdir(template_source_dir):
        print('ERROR: Could not find template at: `%s`, aborting mission.' %
              template_source_dir)
        exit(1)

    # ---------- invariant template source dir exists.

    # Replace template names with app names in all target files
    replacements = {}
    replacements[template_name] = project_name
    replacements[template_name_camelcase_capitalised] = project_name_camelcase_capitalised

    # Create a copy of the template in our target directory.
    # While copying template_name is substituted for project_name
    copy_tree(template_source_dir, app_dir, template_name, project_name,
              ignore=shutil.ignore_patterns('CMakeLists.txt.user', 'build'),
              replacements=replacements)

    # process_directory_recursively(app_dir, replacements)

    print('Done.')
