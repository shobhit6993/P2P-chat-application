To run server

g++ -std=c++11 server.cpp -lpthread -o server
./server

To run client

g++ -std=c++11 -fpermissive -w client.cpp -lpthread -o client

./client 10.0.3.41 stdin

or ./client 10.0.3.41 file

NOTE: the argument to ./client is the IP address of the server.

IMP NOTE: Different clients CANNOT be on the same machine. They MUST be on different machines with different IP for the chat appliacation to function. However, the server, and one of the clients can be on the same machine, in which case that client will be run by command ./client 127.0.0.1

if stdin argument is used, chat will show on terminal although formatting might be bad
if file argument is used The chat is saved in chat.txt and no replies are visible on terminal



$$$$$$$$$$$$$$$$$$$$

The chat is saved in chat.txt which is visible only after the clients exit (due to read permission of files in Linux)$.

$$$$$$$$$$$$$$$$$$$$$

################

SAMPLE OUTPUT IN CHAT

#################

ME#1:sho1

			MSG:1 seen.

	PEER#1:har1

	PEER#2:har2

ME#2:sho2

			MSG:2 seen.

################

ME means the client itself and PEER means the Peer with whom he/she is chatting. #1 denotes the message no. (to verify seen status of that message)

On terminal, you will only see the msg you enter and not the rcvd msgs. However, messages are always rcvd and sent synchronosly. They are not dislplayed on terminal, rather dumped into file so that the order can been shown to be correct. On terminal, if user was entering some msg, while a reply was being rcvd, then the user's msg will flow above in the terminal, leading to confusion. PLEASE CALL ME AT +918011227897 for clarryfication of this argument ANYTIME, before judging the chat application as not user friendly due to file dumping.


FOR TERMINATING EITHER SERVER OR CLIENT PREMATURELY, use CTRL + C, not CTRL + Z, as the latter could leave ports still bound, preventing further execution of server or client.
