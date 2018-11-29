#!/usr/bin/python3

# NOTE: THIS FILE DEPENDS ON AN EXTERNAL PYTHON LIBRARY, PYCPARSER:
# pip install pycparser

import sys, re, tempfile
from os import getenv


# This is not required if you've installed pycparser into
# your site-packages/ with setup.py
#
sys.path.extend(['.', '..'])

from pycparser import c_parser, c_ast, parse_file


def common_from_start(sa, sb):
	""" returns the longest common substring from the beginning of sa and sb """
	def _iter():
		for a, b in zip(sa, sb):
			if a == b:
				yield a
			else:
				return

	return ''.join(_iter())

def to_camel_case(snake_str):
	components = snake_str.split('_')
	# We capitalize the first letter of each component 
	# with the 'capitalize' method and join them together.
	pass1 = ''.join(x.title() for x in components)
	# We need to account for strings which have 'x' between numbers,
	# meaning a stylised multiplication sign, as in "2x2" - this happens
	# when the 'x' is placed between two numbers - as is the case with 
	# with image formats. In this case, we want the "x" lower-cased.
	pass1 = re.sub('([0-9]+[X]{1}[0-9]+)', lambda pat: pat.group(1).lower(), pass1)
	return pass1

def to_upper_snake_case(name):
	s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
	return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).upper()


# A visitor with some state information (the funcname it's looking for)
class EnumVisitor(c_ast.NodeVisitor):
	def __init__(self, enumName, enumAttr='', IsCEnum=False, friendlyName=''):
		self.enumName = enumName
		self.enumAttr = enumAttr
		self.is_c_enum = IsCEnum
		self.indent_level = 0
		self.length_enum_prelude = 0
		self.friendlyName = friendlyName

	def _make_indent(self):
		return '\t' * self.indent_level

	def visit(self, node):
		method = 'visit_' + node.__class__.__name__
		return getattr(self, method, self.generic_visit)(node)

	def generic_visit(self, node):
		#~ print('generic:', type(node))
		if node is None:
			return ''
		else:			
			return ''.join(self.visit(c) for c_name, c in node.children())

	def to_le_enum_name(self, n):
		name = n
		# We must remove the last "_BIT", in case the field was a flag ENUM
		if self.is_c_enum == True:
			return 'LE' + to_upper_snake_case(name[2:])
		else:
			if (name.endswith("_BIT")):
				name = name[:-4]
			return 'e' + to_camel_case(name[self.length_enum_prelude:])

	def visit_Enum(self, n):
		if (n.name == self.enumName):
			return self._generate_enum(n, name='enum')
		else:
			return ''

	def visit_ID(self, n):
		return self.to_le_enum_name(n.name)

	def visit_Enumerator(self, n):
		if (len(n.name) and self.length_enum_prelude == 0):
			c_enum_prelude = to_upper_snake_case(self.enumName)
			self.length_enum_prelude = len(common_from_start(c_enum_prelude, n.name))
		if "MAX_ENUM" in to_upper_snake_case(n.name):
			return '' 
		if "BEGIN_RANGE" in to_upper_snake_case(n.name):
			return '' 
		if "END_RANGE" in to_upper_snake_case(n.name):
			return '' 
		if not n.value:
			return '{indent}{name},\n'.format(
				indent=self._make_indent(),
				name=n.name,
			)
		else:
			if (n.value.__class__.__name__ == "Constant"):
				return '{indent}{name} = {value},\n'.format(
					indent=self._make_indent(),
					name = self.to_le_enum_name(n.name),
					value=n.value.value,
				)
			elif (n.value.__class__.__name__ == "ID"):
				return '{indent}{name} = {value},\n'.format(
					indent=self._make_indent(),
					name = self.to_le_enum_name(n.name),
					value = self.visit(n.value),
				)
			else:
				return ''

	def _generate_enum(self, n, name):
		""" Generates code for enums. name should be
			'enum'.
		"""
		assert name == 'enum'
		members = None if n.values is None else n.values.enumerators
		s = ''

		enumName = ''
		
		# apply any name overrides - otherwise use inferred name

		if self.is_c_enum == False:
			enumName = self.friendlyName or self._remove_vk_prefix(n.name)
			s = name + ' class ' + enumName
		else:
			enumName = self.friendlyName or ('Le' + (self._remove_vk_prefix(n.name)))
			s =  name + ' ' + enumName

		if self.enumAttr != '':
			s += " : %s" % self.enumAttr
		if members is not None:
			# None means no members
			# Empty sequence means an empty list of members
			s += '\n'
			s += self._make_indent()
			self.indent_level += 2
			s += '{\n'
			s += self._generate_enum_body(members)
			self.indent_level -= 2
			s += self._make_indent() + '};\n'			
		return s

	def _generate_enum_body(self, members):
		return ''.join(self.visit(value) for value in members)

	def _remove_vk_prefix(self, name):
		return name[2:]

# This uses the vulkan headers from the system.
# It assumes the Vulkan SDK is installed in default locations.
# 
# Create a temporaty file where we just place the header
# include for the Vulkan Header, so that we may generate an 
# AST form it.
vk_src_file = tempfile.NamedTemporaryFile(suffix='.c')
vk_src_file.write(b'#include <vulkan/vulkan.h>\n')
vk_src_file.seek(0)

ast = parse_file(vk_src_file.name, use_cpp=True,
			cpp_path='gcc',
			cpp_args=['-E', r"-std=c99"])

VkEnumName = ''

if __name__ == "__main__":
	if len(sys.argv) > 1:
		# if called with an enum name as a parameter, 
		# print the generated enum code for
		VkEnumName = sys.argv[1]
		v = EnumVisitor(VkEnumName)
		a = v.visit(ast)
		print (a)
	else:
		# print a test if no parameter specified
		VkEnumName = 'VkAttachmentLoadOp'
		v = EnumVisitor(VkEnumName, 'uint32_t', IsCEnum=True)
		a = v.visit(ast)
		print (a)
		v = EnumVisitor(VkEnumName, 'uint32_t', IsCEnum=False)
		a = v.visit(ast)
		print (a)

