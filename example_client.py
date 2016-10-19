# This is an example for panager json-rpc
# you may copy/redistribute/whatever this file
import json # manipulation of json
import socket # for server connection
import sys

PORT = 1234 # Port that json-rpc server runs
HOST = 'www.linuxxueyuan.com' # Host that the server runs
BUFF = 2048 # Number of bytes to receive from the server socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM) # initialise our socket
sock.connect((HOST, PORT)) # connect to host <HOST> to port <PORT>

# We will store all the input at a dictionary,
# for easier json formatting
input_data = {}
input_data['method'] = sys.argv[1]

dumped_data = json.dumps(input_data) # Dump the input dictionary to proper json
print dumped_data
# Note: dumps faction was imported from json module, remember?

sock.send(dumped_data) # Send the dumped data to the server
server_response = sock.recv(BUFF) # Receive the results (if any) from the server
decoded_response = json.loads(server_response) # decode the data received
sock.close() # close the socket connection
print decoded_response # spit the decoded data
