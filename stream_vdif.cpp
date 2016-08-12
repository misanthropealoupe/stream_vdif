#include <chrono>
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>       
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <thread>
#include <cstring>
#include <ctime>
#include <vector>
#include <thread>

using namespace std;

namespace constants{
	const ssize_t header_size = 32;
	const ssize_t payload_size = 1024;
	const ssize_t packet_size = header_size + payload_size;
	const ssize_t udp_packets = 8;
	const ssize_t rec_size = udp_packets*packet_size;
	const ssize_t burst_mult = 1;
	//const long maxsize = (1000000000l);
	const double native_dt = 2.56*0.000001;
}

template<typename T1> vector<T1> reorder(vector<T1> vec){
    vector<T1> ret = vector<T1>();
	for(typename vector<T1>::iterator it = vec.end() - 1; it >= vec.begin(); --it){
		ret.push_back(*it);
	}
	return ret;
}

//void load_files(long maxsize, unique_ptr<vector<char>> data_ptr, unique_ptr<vector<string>> paths_ptr, unique_ptr<vector<string>> loaded_ptr){
void load_files(long maxsize, vector<char>& data, vector<string>& paths, vector<string>& loaded){
	vector<string> append = vector<string>();
	long totsize = 0;
	string path;
	int fsize;
	cout << paths.size() << endl;
	cout << loaded.size() << endl;
	while(totsize < maxsize){
		if(paths.empty())
			break;

		path = paths.back();
		cout << "loading file: " << path << endl;
		
		ifstream source_file(path, ios::in|ios::binary|ios::ate);
		fsize = (int) source_file.tellg();
		totsize += fsize;
		source_file.seekg(0, ios::beg);
		char* tmp = new char[fsize];
		source_file.read(tmp,fsize);
		source_file.close();
		data.resize(totsize);
		//copy_data
		for(ssize_t i =0; i < fsize; i++){
			data[i + totsize - fsize] = tmp[i];
		}
		delete[] tmp;
		append.push_back(path);
		paths.pop_back();
	}
	append = reorder(append);
	loaded.insert(loaded.end(),append.begin(),append.end());
	cout << paths.size() << endl;
	cout << loaded.size() << endl;
}

int main(int argc, char* argv[]){
	// for(int i = 0; i < argc; i++){
	// 	cout << argv[i] << endl;
	// }
	if(strcmp(argv[1],"-h") == 0){
		cout << "syntax: <filepath> <dest_addr> <dest_port> <nsec> <buffer size MB>" << endl;
		exit(1);
	}

	//load all files into memory
	vector<string> paths = vector<string>();
	for(int i = 1; i < argc - 4; i++){
		paths.push_back(string(argv[i]));
	}
	paths = reorder(paths);

	char* dest_addr = argv[argc - 4];
	int port = atoi(argv[argc - 3]);
	cout << "attempting to connect " << dest_addr << ":" << port << endl;
	
	int sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock_fd < 0) {
		cout << "socket failed." << endl;
		exit(1);
	}

	struct sockaddr_in server_address;

	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr.s_addr = inet_addr(dest_addr);
	if (connect(sock_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
		cout << "connect failed." << endl;
		exit(1);
	}
	cout << "successfully connected socket" << endl;

	int nsec = atoi(argv[argc - 2]);

	int nmb = atoi(argv[argc - 1]);
	long maxsize = ((long) nmb)*1024l*1024l;

	cout << "loading initial buffer" << endl;
	vector<string> loaded = vector<string>();

	vector<char> alternate_buf = {};
	vector<char> active_buf = {};
	load_files(maxsize,active_buf,paths,loaded);

	cout << "starting write loop for " << nsec << " seconds of data" << endl;

	double since_last_send;
	double time_elapsed = 0;
	double nsecf = (double) nsec;
	bool forever = nsec < 1;
	if(forever){
		cout << "streaming data forever" << endl;
	}
	double nsec_to_sec = 1.0/1000000000;
	double nnsec_per_write =(double) (((double) constants::udp_packets*constants::burst_mult)*constants::native_dt/2.0)*1000000000.0;
	cout << "nanoseconds per write: " << nnsec_per_write << endl;
	using nanosec = chrono::duration<double, nano>;
	chrono::time_point<chrono::system_clock> start_time = chrono::system_clock::now();

	long sends = 0;
	long last_ind = 0;
	int interval = 200;
	int delay_interval = 10;
	int nwrite = 0;
	const int write_size = constants::udp_packets*constants::packet_size*constants::burst_mult;
	cout << "write size: " << write_size << endl;
	chrono::time_point<chrono::system_clock> last_send = chrono::system_clock::now();
	chrono::time_point<chrono::system_clock> this_send;
	double accumulated_realtime = 0;
	double accumulated_sendtime = 0;

	long active_size;
	long pos;
	while(!paths.empty() || forever){
		active_size = active_buf.size();
		pos = 0;
		thread load_t(load_files,maxsize,ref(alternate_buf),ref(paths),ref(loaded));
		while(active_size - pos > 0){
			nwrite = send(sock_fd,(void*) &(active_buf[last_ind]),write_size, MSG_DONTWAIT);
			if(last_ind != pos)
				cout << "wrapped around" << endl;
			
			last_ind = (last_ind + write_size) % active_size;
			pos += write_size;
			// if(nwrite < 0){
			// 	cout << "error writing to socket" << endl;
			// 	//exit(1);
			// }
		

			this_send = chrono::system_clock::now();
			accumulated_sendtime += (double) chrono::duration_cast<nanosec>(this_send - last_send).count();
			last_send = this_send;
			time_elapsed += nnsec_per_write*nsec_to_sec;
			accumulated_realtime += nnsec_per_write;

			if(sends % delay_interval == delay_interval - 1){
				this_thread::sleep_for(nanosec(max(0.0,accumulated_realtime - accumulated_sendtime)));
				accumulated_realtime = 0;
				accumulated_sendtime = 0;
			}
			sends++;
			// if(sends % interval == 0){
			// 	//cout << "progress: " << time_elapsed/nsecf << endl;
			// }
		}
		cout << "exhausted buffer" << endl;
		load_t.join();
		cout << "loaded new buffer" << endl;
		active_buf = alternate_buf;
		alternate_buf = vector<char>();
	}
	//delete[] data;
	double total_time = chrono::duration_cast<nanosec>(chrono::system_clock::now() - start_time).count()*nsec_to_sec;
	cout << "took " << total_time << " to send " << nsecf << endl;
	close(sock_fd);
}