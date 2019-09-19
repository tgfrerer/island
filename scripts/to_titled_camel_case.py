#!/usr/bin/python3
import sys

# simple utility which will take a string from stdin 
# and will return the string trannsformed into titled
# camelcase.

def to_titled_camel_case(snake_str):
	components = snake_str.split('_')
	# We capitalize the first letter of each component.
	return ''.join(x.title() for x in components)

for line in sys.stdin:
    print(to_titled_camel_case(line),end="")