/*************************************************
  Kaiju

  Authors: Peter Menzel <pmenzel@gmail.com> and
           Anders Krogh <krogh@binf.ku.dk>

  Copyright (C) 2015-2021 Peter Menzel and Anders Krogh

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program, see file LICENSE.
  If not, see <http://www.gnu.org/licenses/>.

  See the file README.md for documentation.
**************************************************/

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <deque>
#include <stdexcept>

#include "ProducerConsumerQueue/src/ProducerConsumerQueue.hpp"
#include "zstr/zstr.hpp"
#include "ReadItem.hpp"
#include "ConsumerThread.hpp"
#include "Config.hpp"
#include "util.hpp"

extern "C" {
#include "./bwt/bwt.h"
}


void usage(char *progname);

int main(int argc, char** argv) {


	Config * config = new Config();

	std::unordered_map<uint64_t,uint64_t> * nodes = new std::unordered_map<uint64_t,uint64_t>();

	std::string nodes_filename;
	std::string fmi_filename;
	std::string sa_filename;
	std::string in1_filename;
	std::string in2_filename;
	std::string output_filename;

	int num_threads = 1;
	bool verbose = false;
	bool debug = false;
	bool paired  = false;

	// --------------------- START ------------------------------------------------------------------
	// Read command line params
	int c;
	while ((c = getopt(argc, argv, "a:hdpxXvn:m:e:E:l:t:f:i:j:s:z:o:")) != -1) {
		switch (c)  {
			case 'a': {
									if("mem" == std::string(optarg)) config->mode = MEM;
									else if("greedy" == std::string(optarg)) config->mode = GREEDY;
									else { std::cerr << "-a must be a valid mode.\n"; usage(argv[0]); }
									break;
								}
			case 'h':
				usage(argv[0]);
			case 'd':
				debug = true; break;
			case 'v':
				verbose = true; break;
			case 'p':
				config->input_is_protein = true; break;
			case 'x':
				config->SEG = true; break;
			case 'X':
				config->SEG = false; break;
			case 'o':
				output_filename = optarg; break;
			case 'f':
				fmi_filename = optarg; break;
			case 't':
				nodes_filename = optarg; break;
			case 'i':
				in1_filename = optarg; break;
			case 'j': {
									in2_filename = optarg;
									paired = true;
									break;
								}
			case 'l': {
									try {
										int seed_length = std::stoi(optarg);
										if(seed_length < 7) { error("Seed length must be >= 7."); usage(argv[0]); }
										config->seed_length = (unsigned int)seed_length;
									}
									catch(const std::invalid_argument& ia) {
										std::cerr << "Invalid argument in -l " << optarg << std::endl;
									}
									catch (const std::out_of_range& oor) {
										std::cerr << "Invalid argument in -l " << optarg << std::endl;
									}
									break;
								}
			case 's': {
									try {
										int min_score = std::stoi(optarg);
										if(min_score <= 0) { error("Min Score (-s) must be greater than 0."); usage(argv[0]); }
										config->min_score = (unsigned int)min_score;
									}
									catch(const std::invalid_argument& ia) {
										std::cerr << "Invalid argument in -s " << optarg << std::endl;
									}
									catch (const std::out_of_range& oor) {
										std::cerr << "Invalid argument in -s " << optarg << std::endl;
									}
									break;
								}
			case 'm': {
									try {
										int min_fragment_length = std::stoi(optarg);
										if(min_fragment_length <= 0) { error("Min fragment length (-m) must be greater than 0."); usage(argv[0]); }
										config->min_fragment_length = (unsigned int)min_fragment_length;
									}
									catch(const std::invalid_argument& ia) {
										std::cerr << "Invalid argument in -m " << optarg << std::endl;
									}
									catch (const std::out_of_range& oor) {
										std::cerr << "Invalid argument in -m " << optarg << std::endl;
									}
									break;
								}
			case 'e': {
									try {
										int mismatches = std::stoi(optarg);
										if(mismatches < 0) { error("Number of mismatches must be >= 0."); usage(argv[0]); }
										config->mismatches = (unsigned int)mismatches;
									}
									catch(const std::invalid_argument& ia) {
										std::cerr << "Invalid numerical argument in -e " << optarg << std::endl;
									}
									catch (const std::out_of_range& oor) {
										std::cerr << "Invalid numerical argument in -e " << optarg << std::endl;
									}
									break;
								}
			case 'E': {
									try {
										config->min_Evalue = std::stod(optarg);
										if(config->min_Evalue <= 0.0) { error("E-value threshold must be greater than 0."); usage(argv[0]); }
										config->use_Evalue = true;
									}
									catch(const std::invalid_argument& ia) {
										std::cerr << "Invalid numerical argument in -E " << optarg << std::endl;
									}
									catch (const std::out_of_range& oor) {
										std::cerr << "Invalid numerical argument in -E " << optarg << std::endl;
									}
									break;
								}
			case 'z': {
									try {
										num_threads = std::stoi(optarg);
										if(num_threads <= 0) {  error("Number of threads (-z) must be greater than 0."); usage(argv[0]); }
									}
									catch(const std::invalid_argument& ia) {
										std::cerr << "Invalid argument in -z " << optarg << std::endl;
									}
									catch (const std::out_of_range& oor) {
										std::cerr << "Invalid argument in -z " << optarg << std::endl;
									}
									break;
								}
			default:
								usage(argv[0]);
		}
	}
	if(nodes_filename.length() == 0) { error("Please specify the location of the nodes.dmp file, using the -t option."); usage(argv[0]); }
	if(fmi_filename.length() == 0) { error("Please specify the location of the FMI file, using the -f option."); usage(argv[0]); }
	if(in1_filename.length() == 0) { error("Please specify the location of the input file, using the -i option."); usage(argv[0]); }
	if(paired && config->input_is_protein) { error("Protein input only supports one input file."); usage(argv[0]); }
	if(config->use_Evalue && config->mode != GREEDY ) { error("E-value calculation is only available in Greedy mode. Use option: -a greedy"); usage(argv[0]); }

	if(debug) {
		std::cerr << "Parameters: \n";
		std::cerr << "  minimum match length: " << config->min_fragment_length << "\n";
		std::cerr << "  minimum blosum62 score for matches: " << config->min_score << "\n";
		std::cerr << "  seed length for greedy matches: " << config->seed_length << "\n";
		if(config->use_Evalue)
			std::cerr << "  minimum E-value: " << config->min_Evalue << "\n";
		std::cerr << "  max number of mismatches within a match: "  << config->mismatches << "\n";
		std::cerr << "  run mode: "  << ((config->mode==MEM) ? "MEM" : "Greedy") << "\n";
		std::cerr << "  input files 1: " << in1_filename << "\n";
		if(in2_filename.length() > 0)
			std::cerr << "  input files 2: " << in2_filename << "\n";
		std::cerr << "  output files: " << output_filename << "\n";
	}

	/* parse lists of input files and output files and sanity-check */
	std::vector<std::string> fname1_list;
	size_t begin = 0;
	size_t pos = -1;
	std::string fname;
	while((pos = in1_filename.find(",",pos+1)) != std::string::npos) {
		fname = in1_filename.substr(begin,(pos - begin));
		if(fname.length()==0 || fname==",") { begin=pos+1; continue; }
		fname1_list.emplace_back(fname);
		begin = pos+1;
	}
	fname = in1_filename.substr(begin);
	if(!(fname.length()==0 || fname==",")) {
		fname1_list.emplace_back(fname);
	}

	std::vector<std::string> fname2_list;
	begin = 0;
	pos = -1;
	while((pos = in2_filename.find(",",pos+1)) != std::string::npos) {
		fname = in2_filename.substr(begin,(pos - begin));
		if(fname.length()==0 || fname==",") { begin=pos+1; continue; }
		fname2_list.emplace_back(fname);
		begin = pos+1;
	}
	fname = in2_filename.substr(begin);
	if(!(fname.length()==0 || fname==",")) {
		fname2_list.emplace_back(fname);
	}

	std::vector<std::string> fname_out_list;
	begin = 0;
	pos = -1;
	while((pos = output_filename.find(",",pos+1)) != std::string::npos) {
		fname = output_filename.substr(begin,(pos - begin));
		if(fname.length()==0 || fname==",") { begin=pos+1; continue; }
		fname_out_list.emplace_back(fname);
		begin = pos+1;
	}
	fname = output_filename.substr(begin);
	if(!(fname.length()==0 || fname==",")) {
		fname_out_list.emplace_back(fname);
	}

	// check that all three file lists have the same length
	if(output_filename.length() > 0 && (
			paired & (fname1_list.size() != fname2_list.size() || fname1_list.size() != fname_out_list.size()) ||
		 !paired & (fname1_list.size() != fname_out_list.size())
		 ) || output_filename.length() == 0 && paired && (fname1_list.size() != fname2_list.size())) {
		error("Length of input/output file lists differs");
		exit(1);
	}

	// check if all input files are readable
	for(auto const & f : fname1_list) {
		std::ifstream test_file;
		test_file.open(f.c_str());
		if(!test_file.is_open()) { error("Could not open file " + f); exit(EXIT_FAILURE); }
		test_file.close();
	}
	for(auto const & f : fname2_list) {
		std::ifstream test_file;
		test_file.open(f.c_str());
		if(!test_file.is_open()) { error("Could not open file " + f); exit(EXIT_FAILURE); }
		test_file.close();
	}

	config->nodes = nodes;
	config->debug = debug;
	config->verbose = verbose;

	if(verbose) std::cerr << getCurrentTime() << " Reading database" << std::endl;

	std::ifstream nodes_file;
	nodes_file.open(nodes_filename.c_str());
	if(!nodes_file.is_open()) { error("Could not open file " + nodes_filename); exit(EXIT_FAILURE); }
	if(verbose) std::cerr << " Reading taxonomic tree from file " << nodes_filename << std::endl;
	parseNodesDmp(*nodes,nodes_file);
	nodes_file.close();

	readFMI(fmi_filename,config);

	config->init();
	config->out_stream = &std::cout;

	//iterate through input files
	for(int i_files = 0; i_files < fname1_list.size(); i_files++) {

		std::string fname_in1, fname_in2, fname_out;
		fname_in1 = fname1_list.at(i_files);
		if(paired) fname_in2 = fname2_list.at(i_files);

		if(verbose) {
			std::cerr << getCurrentTime() << " Processing input file " << fname_in1 ;
			if(paired) std::cerr << " and " << fname_in2 ;
			std::cerr << std::endl;
		}

		if(output_filename.length() > 0) {
			fname_out = fname_out_list.at(i_files);
			if(verbose) std::cerr << getCurrentTime() <<  " Output file: " << fname_out << std::endl;
			std::ofstream * read2id_file = new std::ofstream();
			read2id_file->open(fname_out);
			if(!read2id_file->is_open()) {  error("Could not open file " + fname_out + " for writing"); exit(EXIT_FAILURE); }
			config->out_stream = read2id_file;
		}

		ProducerConsumerQueue<ReadItem*>* myWorkQueue = new ProducerConsumerQueue<ReadItem*>(500);
		std::deque<std::thread> threads;
		std::deque<ConsumerThread *> threadpointers;
		for(int i=0; i < num_threads; i++) {
			ConsumerThread * p = new ConsumerThread(myWorkQueue, config);
			threadpointers.push_back(p);
			threads.push_back(std::thread(&ConsumerThread::doWork,p));
		}

		zstr::ifstream* in1_file = nullptr;
		zstr::ifstream* in2_file = nullptr;
		try {
			in1_file = new zstr::ifstream(fname_in1);
			if(!in1_file->good()) {  error("Could not open file " + fname_in1); exit(EXIT_FAILURE); }
		} catch(std::exception e) { error("Could not open file " + fname_in1); exit(EXIT_FAILURE); }

		if(paired) {
			try {
				in2_file = new zstr::ifstream(fname_in2);
				if(!in2_file->good()) {  error("Could not open file " + fname_in2); exit(EXIT_FAILURE); }
			} catch(std::exception e) { error("Could not open file " + fname_in2); exit(EXIT_FAILURE); }
		}

		bool firstline_file1 = true;
		bool firstline_file2 = true;
		bool isFastQ_file1 = false;
		bool isFastQ_file2 = false;
		std::string line_from_file;
		line_from_file.reserve(2000);
		std::string suffixStartCharacters = " /\t\r";
		std::string name;
		std::string sequence1;
		std::string sequence2;
		sequence1.reserve(2000);
		if(paired) sequence2.reserve(2000);


		while(getline(*in1_file,line_from_file)) {
			if(line_from_file.length() == 0) { continue; }
			if(firstline_file1) {
				char fileTypeIdentifier = line_from_file[0];
				if(fileTypeIdentifier == '@') {
					isFastQ_file1 = true;
				}
				else if(fileTypeIdentifier != '>') {
					error("Auto-detection of file type for file " + fname_in1 + " failed.");
					exit(EXIT_FAILURE);
				}
				firstline_file1 = false;
			}
			if(isFastQ_file1) {
				// remove '@' from beginning of line
				line_from_file.erase(line_from_file.begin());
				// delete suffixes like '/1' or ' 1:N:0:TAAGGCGA' from end of read name
				size_t n = line_from_file.find_first_of(suffixStartCharacters);
				if(n != std::string::npos) { line_from_file.erase(n); }
				name = line_from_file;
				// read sequence line
				getline(*in1_file,line_from_file);
				sequence1 = line_from_file;
				// skip + lin
				in1_file->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
				// skip quality score line
				in1_file->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			}
			else { //FASTA
				// remove '>' from beginning of line
				line_from_file.erase(line_from_file.begin());
				// delete suffixes like '/1' or ' 1:N:0:TAAGGCGA' from end of read name
				size_t n = line_from_file.find_first_of(suffixStartCharacters);
				if(n != std::string::npos) { line_from_file.erase(n); }
				name = line_from_file;
				// read lines until next entry starts or file terminates
				sequence1.clear();
				while(!(in1_file->peek()=='>' || in1_file->peek()==EOF)) {
					getline(*in1_file,line_from_file);
					sequence1.append(line_from_file);
				}
			} // end FASTA

			strip(sequence1); // remove non-alphabet chars

			if(paired) {
				line_from_file = "";
				while(line_from_file.length() == 0) {
					if(!getline(*in2_file,line_from_file)) {
						//that's the border case where file1 has more entries than file2
						error("File " + fname_in1 + " contains more reads then file " + in2_filename);
						exit(EXIT_FAILURE);
					}
				}
				if(firstline_file2) {
					char fileTypeIdentifier = line_from_file[0];
					if(fileTypeIdentifier == '@') {
						isFastQ_file2 = true;
					}
					else if(fileTypeIdentifier != '>') {
						error("Auto-detection of file type for file " + fname_in2 + " failed.");
						exit(EXIT_FAILURE);
					}
					firstline_file2 = false;
				}
				if(isFastQ_file2) {
					// remove '@' from beginning of line
					line_from_file.erase(line_from_file.begin());
					// delete suffixes like '/2' or ' 2:N:0:TAAGGCGA' from end of read name
					size_t n = line_from_file.find_first_of(suffixStartCharacters);
					if(n != std::string::npos) { line_from_file.erase(n); }
					if(name != line_from_file) {
						error("Read names are not identical between the two input files. Probably reads are not in the same order in both files.");
						exit(EXIT_FAILURE);
					}
					// read sequence line
					getline(*in2_file,line_from_file);
					sequence2 = line_from_file;
					// skip + line
					in2_file->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
					// skip quality score line
					in2_file->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
				}
				else { // FASTA
					// remove '>' from beginning of line
					line_from_file.erase(line_from_file.begin());
					// delete suffixes like '/2' or ' 2:N:0:TAAGGCGA' from end of read name
					size_t n = line_from_file.find_first_of(suffixStartCharacters);
					if(n != std::string::npos) { line_from_file.erase(n); }
					if(name != line_from_file) {
						std::cerr << "Error: Read names are not identical between the two input files" << std::endl;
						exit(EXIT_FAILURE);
					}
					sequence2.clear();
					while(!(in2_file->peek()=='>' || in2_file->peek()==EOF)) {
						getline(*in2_file,line_from_file);
						sequence2.append(line_from_file);
					}
				}
				strip(sequence2); // remove non-alphabet chars
				myWorkQueue->push(new ReadItem(name, sequence1, sequence2));
			} // not paired
			else {
				myWorkQueue->push(new ReadItem(name, sequence1));
			}

		} // end main loop around file1

		myWorkQueue->pushedLast();

		delete in1_file;

		if(paired && in2_file->good()) {
			if(getline(*in2_file,line_from_file) && line_from_file.length()>0) {
				std::cerr << "Warning: File " << fname_in2 <<" has more reads then file " << fname_in1  <<std::endl;
			}
			delete in2_file;
		}

		while(!threads.empty()) {
			threads.front().join();
			threads.pop_front();
			delete threadpointers.front();
			threadpointers.pop_front();
		}

		config->out_stream->flush();
		if(output_filename.length()>0) {
			((std::ofstream*)config->out_stream)->close();
			delete ((std::ofstream*)config->out_stream);
		}

		delete myWorkQueue;

	} // end loop around file list

	if(verbose) std::cerr << getCurrentTime() << " Finished." << std::endl;


	delete config;
	delete nodes;
	return EXIT_SUCCESS;
}

void usage(char *progname) {
	print_usage_header();
	fprintf(stderr, "Usage:\n   %s -t nodes.dmp -f kaiju_db.fmi -i sample1_R1.fastq,sample2_R1.fastq [-j sample1_R2.fastq,sample2_R2.fastq] -o sample1.out,sample2.out\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Mandatory arguments:\n");
	fprintf(stderr, "   -t FILENAME   Name of nodes.dmp file\n");
	fprintf(stderr, "   -f FILENAME   Name of database (.fmi) file\n");
	fprintf(stderr, "   -i FILENAME   List of input files containing reads in FASTA or FASTQ format\n");
	fprintf(stderr, "   -o FILENAME   List of output files \n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Optional arguments:\n");
	fprintf(stderr, "   -j FILENAME   List of secondary input files for paired-end reads\n");
	fprintf(stderr, "   -z INT        Number of parallel threads for classification (default: 1)\n");
	fprintf(stderr, "   -a STRING     Run mode, either \"mem\"  or \"greedy\" (default: greedy)\n");
	fprintf(stderr, "   -e INT        Number of mismatches allowed in Greedy mode (default: 3)\n");
	fprintf(stderr, "   -m INT        Minimum match length (default: 11)\n");
	fprintf(stderr, "   -s INT        Minimum match score in Greedy mode (default: 65)\n");
	fprintf(stderr, "   -E FLOAT      Minimum E-value in Greedy mode\n");
	fprintf(stderr, "   -x            Enable SEG low complexity filter (enabled by default)\n");
	fprintf(stderr, "   -X            Disable SEG low complexity filter\n");
	fprintf(stderr, "   -p            Input sequences are protein sequences\n");
	fprintf(stderr, "   -v            Enable verbose output\n");
	//fprintf(stderr, "   -d            Enable debug output.\n");
	exit(EXIT_FAILURE);
}

