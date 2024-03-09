# Client Server Chat Room

### Note: This chat room is still in progress, this application is in need of a communication protocol and more robost error handling

## Description
- This is a client server text chatroom.
- The chat room allows for a few different basic commands, one of which is /whisper, which allows a user to direct message another user by username in the chat room.
- The command: /commands, may be used by either the client or the server to list all available commands
  
## Installation

### Prerequisites
- The program(both client and server) must be run on a Windows version that supports the Winsock api.
- Ensure you have a C++ compiler installed, such as [GCC](https://gcc.gnu.org/) or [Clang](https://clang.llvm.org/), or an IDE that supports C++ such as [Visual Studio](https://visualstudio.microsoft.com/).

 ### Compiling the Program
- Clone the repository to your local machine or download the source files.
- Open the Command Prompt or terminal window in the directory where the source files are located.
- Use the C++ compiler to compile the source code. For example, with GCC, the command would be: g++ -o ServerChatApp server.cpp -std=c++11
- If using an IDE similar to Visual Studio, compile and run the program using F5 or the Run button. 

### Running the Program
- Both the client and server programs may be ran on the same or different machine.
- To connect to the server machine, the client must enter the server's IPv4 address.
- To run the client side application repeat the above steps in the directory where the Client side's client.cpp is saved.
- If your machine is protected behind a firewall or IDS, administrator approval may be required to allow network connections.

## Troubleshooting
- If you encounter issues with network connectivity, ensure that the correct port is open and not blocked by your firewall.
- The client and server programs have port 50000 hardcoded to listen and connect on, this can be changed in their respective .h files.
