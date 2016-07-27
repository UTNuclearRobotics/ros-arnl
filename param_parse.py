#!/usr/bin/python

# Usage: python param_parse.py [filename]
# The script looks in /usr/local/Arnl/params. No need to specify full path.

import sys

global namespace
namespace = ""

def parseLine(line):
  global namespace
  
  line.strip()
  
  # Search for comment character ';'
  comment = ""
  comment_index = line.find(';')
  if comment_index != -1:
    comment = line[comment_index:]
    line = line[0:comment_index]
  
  # Check for a section header
  if line.find("Section") == 0:
    index = len("Section")
    namespace = line[index:].strip()
    namespace = namespace.replace(' ', '_')
    return "\n"
  else:
    # Separate the label and value
    index = line.find(' ')
    if index != -1:
      label = line[0:index].strip() + ':'
      label = label.replace(' ', '_')
      
      value = line[(index+1):].strip()
      
      if not label or not value:
	return ""
      
      if not isNumber(value) and not isBool(value):
	value = "'" + value + "'"
      
      output = ""
      if namespace:
	output = namespace + "/"
      output += label + " " + value + '\n'
      return output
      
    else:
      return ""
    
def isNumber(s):
  try:
    float(s)
    return True
  except ValueError:
    return False
  
def isBool(s):
  return s == "true" or s == "false"

# Get the total number of args passed to the demo.py
num_args = len(sys.argv)

if num_args != 2:
  sys.exit("Usage: python param_parse.py [filepath]")
 
# Get the file path argument
input_filepath = "/usr/local/Arnl/params/" + str(sys.argv[1])
 
# Open the input file with read only permit
try:
  input_file = open(input_filepath, 'r')
except IOError:
   sys.exit("Could not open file: %s" % input_filepath)
  
# Open the output file with write permit
output_file_name = "parse_output.yaml"
try:
  output_file = open(output_file_name, 'w')
except IOError:
   sys.exit("Could not open file: %s" % output_file_name)

# Parse line by line
for line in iter(input_file):
  #parse
  parsed_line = parseLine(line)
  
  # add to xml file
  output_file.write(parsed_line)

# Close files
input_file.close()
output_file.close()