# ICMP_XEROX
Сopying a file via ICMP(Echo request)  
Proof of concept.  
Usage:  
1. Start server ./icmp_server.bin  
*You can disable the system echo response (optional)  
Edit /etc/sysctl.conf  
Add the following line to your /etc/sysctl.conf:  
net.ipv4.icmp_echo_ignore_all=1  
Then:  
sysctl -p*  
2. Start copying the file  
./icmp_copy_file_client.bin -h 192.168.0.5 -f hello.bin  