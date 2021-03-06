#include<stdio.h>
#include<string.h>
#include<assert.h>
#include<sys/time.h>
#include<linux/if_packet.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<net/if.h>
#include<net/ethernet.h>
#include<net/if_arp.h>
#include<netinet/in.h>
#include<netinet/ip.h>
#include<netinet/ip_icmp.h>
#include<arpa/inet.h>
#include<errno.h>

int sockfd = -1;
int addr_len = sizeof(struct sockaddr_ll);
char send_buf[256];
char recv_buf[256];
struct sockaddr_ll dest_ll;
struct sockaddr_ll src_ll;
struct sockaddr_in dest_in;
struct ip *ip_header;
struct icmp *icmp_header;
struct in_addr destination;
char gateway[16];
struct in_addr netmask;
char interface[14];

struct route_item{
	char destination[16];
	char gateway[16];
	char netmask[16];
	char interface[16];
}route_info[3];

struct arp_table_item{
	char ip_addr[16];
	char mac_addr[18];
}arp_table[2];

struct device_item{
	char interface[14];
	char ip_addr[16];
	char mac_addr[18];
}device[2];

//初始化表项 
void init()
{
	FILE *file=fopen("Router2.txt","r");
	int i;
	for(i = 0; i < 3; i++)
	{
		fscanf(file,"%s",route_info[i].destination);
		fscanf(file,"%s",route_info[i].gateway);
		fscanf(file,"%s",route_info[i].netmask);
		fscanf(file,"%s",route_info[i].interface);
	}
	for(i = 0; i < 2; i++)
	{
		fscanf(file,"%s",arp_table[i].ip_addr);
		fscanf(file,"%s",arp_table[i].mac_addr);
	}
	for(i = 0; i < 2; i++)
	{
		fscanf(file,"%s",device[i].interface);
		fscanf(file,"%s",device[i].ip_addr);
		fscanf(file,"%s",device[i].mac_addr);
	}
	fclose(file);
}

//校验和算法
int checksum(unsigned short *buf, int len)
{    
	int nleft = len;    
	int sum = 0;    
	unsigned short *w = (unsigned short *)buf;    
	unsigned short answer = 0;   
	//把ICMP报头二进制数据以2字节为单位累加起来 
	while (nleft > 1)    
	{       
		sum += *w++;       
		nleft -= 2;    
	}     
	//若ICMP报头为奇数个字节，会剩下最后一字节。把最后一个字节视为一个2字节数据的高字节，这个2字节数据的低字节为0，继续累加    
	if (nleft == 1)    
	{            
	    *((unsigned char *)&answer) = *((unsigned char *)w);        
		sum += answer;    
	}     
	sum = (sum>>16) + (sum & 0xffff);    
	sum += (sum >> 16);    
	answer = ~sum;     
	return answer;
}

//获得物理接口信息 
int getifindex(char *if_name)
{
	int temp = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	struct ifreq req;
	memset(&req, 0, sizeof(req));
	strncpy(req.ifr_name, if_name, IFNAMSIZ - 1);
	ioctl(temp, SIOCGIFINDEX, &req);
	return req.ifr_ifindex;
}

//解封 
int unpack()
{
    ip_header = (struct ip *)recv_buf;
    icmp_header = (struct icmp *)(recv_buf + ip_header->ip_hl * 4);
    if(icmp_header->icmp_type == ICMP_ECHO)
    {
	    unsigned char *p = src_ll.sll_addr;
		printf("receive an ICMP request packet from %.2x:%02x:%02x:%02x:%02x:%02x\n",p[0],p[1],p[2],p[3],p[4],p[5]);
	    printf("src: %s, ",inet_ntoa(ip_header->ip_src));
		printf("dst: %s\n",inet_ntoa(ip_header->ip_dst));
		int i; 
	    for(i = 0; i < 2; i++)
	    	if(strcmp(inet_ntoa(ip_header->ip_dst),device[i].ip_addr) == 0)
	    		break;
    	return i;
    }
    else if(icmp_header->icmp_type == ICMP_ECHOREPLY)
    {
    	unsigned char *p = src_ll.sll_addr;
		printf("receive an ICMP reply packet from %.2x:%02x:%02x:%02x:%02x:%02x\n",p[0],p[1],p[2],p[3],p[4],p[5]);
	    printf("src: %s, ",inet_ntoa(ip_header->ip_src));
		printf("dst: %s\n",inet_ntoa(ip_header->ip_dst));
		int i; 
	    for(i = 0; i < 2; i++)
	    	if(strcmp(inet_ntoa(ip_header->ip_dst),device[i].ip_addr) == 0)
	    		break;
    	return i;
	}
	else 
		return -1;
}

//填充dest_in,dest_ll必要信息
void fill()
{
	int i;
	//填充dest_in 
	dest_in.sin_family = AF_INET;
	dest_in.sin_addr = ip_header->ip_dst;

	//填充dest_ll 
	dest_ll.sll_family = AF_PACKET;
	dest_ll.sll_protocol = htons(ETH_P_IP);
	dest_ll.sll_halen = ETH_ALEN;
	for(i = 0; i < 3; i++)
	{
		inet_aton(route_info[i].destination, &destination);
		strncpy(gateway,route_info[i].gateway,16);
		inet_aton(route_info[i].netmask, &netmask);
		strncpy(interface,route_info[i].interface,14);
		if((dest_in.sin_addr.s_addr&netmask.s_addr) == destination.s_addr)
			break;
	} 
	assert(i < 3);
	dest_ll.sll_ifindex = getifindex(interface);
	if(strcmp(gateway,"0.0.0.0") == 0)
		strncpy(gateway,inet_ntoa(dest_in.sin_addr),16);
	for(i = 0; i < 2; i++)
		if(strcmp(gateway,arp_table[i].ip_addr) == 0)
			break;
	//assert(i < 2);
	unsigned int temp[6];
	sscanf(arp_table[i].mac_addr,"%2x:%2x:%2x:%2x:%2x:%2x",&temp[0],&temp[1],&temp[2],&temp[3],&temp[4],&temp[5]);
	for(i = 0;i < 6; i++)
		dest_ll.sll_addr[i] = (unsigned char)temp[i];
	dest_ll.sll_addr[6] = dest_ll.sll_addr[7] = 0x00;
}

//响应 
void reply()
{
	memcpy(send_buf,recv_buf,256);
	ip_header = (struct ip *)send_buf;
	struct in_addr temp;
	temp = ip_header->ip_dst;
	ip_header->ip_dst = ip_header->ip_src;
	ip_header->ip_src = temp; 
	icmp_header = (struct icmp *)(send_buf + ip_header->ip_hl * 4);
	icmp_header->icmp_type = ICMP_ECHOREPLY; 
	icmp_header->icmp_cksum = 0;
	icmp_header->icmp_cksum = checksum((unsigned short *)icmp_header, 64); 
	printf("reply to it\n\n");
	assert(sendto(sockfd, send_buf, 64, 0, (struct sockaddr *)&src_ll, sizeof(struct sockaddr_ll)) > 20);
}

//转发
void forward()
{
	memcpy(send_buf,recv_buf,256);
	ip_header = (struct ip *)send_buf; 
	ip_header->ip_ttl--;
	ip_header->ip_sum = 0;
	ip_header->ip_sum = checksum((unsigned short *)ip_header, 20);
	icmp_header = (struct icmp *)(send_buf + ip_header->ip_hl * 4);
	icmp_header->icmp_cksum = 0;
	icmp_header->icmp_cksum = checksum((unsigned short *)icmp_header, 64);
	fill();
	unsigned char *p = dest_ll.sll_addr;
	printf("forward it to (%.2x:%02x:%02x:%02x:%02x:%02x)\n\n",p[0],p[1],p[2],p[3],p[4],p[5]);
	assert(sendto(sockfd, send_buf, 64, 0, (struct sockaddr *)&dest_ll, sizeof(struct sockaddr_ll)) > 20);
} 

int main(int argc, char *argv[])
{
	init();
	sockfd = socket(AF_PACKET,SOCK_DGRAM,htons(ETH_P_IP));
	assert(sockfd >= 0);
	src_ll.sll_family = AF_PACKET;
	src_ll.sll_protocol = htons(ETH_P_IP);
	src_ll.sll_halen = ETH_ALEN;
	while(1)
	{
		if(recvfrom(sockfd, recv_buf, 64, 0, (struct sockaddr *)&src_ll, &addr_len) > 0)
		{
			if((src_ll.sll_hatype == ARPHRD_ETHER)&&(src_ll.sll_pkttype == PACKET_HOST))
			{
				int i = unpack(); 
				if(i == 2)
					forward();
				else if(i == 0 || i == 1)
					reply();
			}
		}
	}
}
