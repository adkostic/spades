#!/usr/bin/python -O

#Calculate coverage from raw file

import sys
import os
import datetime
import getopt
#from joblib import Parallel, delayed
from math import pow;
#profile = []
#insertions = {}
config = {}


def read_genome(filename):
    res_seq = []

    seq =''
    for line in open(filename):
            if line[0] == '>':
    	    	    res_seq.append(line[1:])
            else:
    	    	    seq += line.strip()
    res_seq.append(seq)
    return res_seq


def read_contigs(filename):
    res_seq = {}

    seq =''
    cont_name = ''
    for line in open(filename):
        if line[0] == '>':
            if cont_name != '':
                res_seq[cont_name] = seq
            seq = ''
            cont_name = line[1:].strip()
        else:
            seq += line.strip()
    if cont_name != '':
        res_seq[cont_name] = seq
    return res_seq


def write_fasta(data, filename):
    outFile = open(filename, 'w')
    for seq in data:
            outFile.write('>' + seq[0].strip() + '\n' );
            i = 0
            while i < len(seq[1]):
                    outFile.write(seq[1][i:i+60] + '\n')
                    i += 60
    outFile.close()


def vote_insertions(position, insertions):
#    global insertions;
    arr = insertions[position]
    lengths = {}
    for strn in arr:
        if len(strn) not in lengths:
            lengths[len(strn)] = 0;
        lengths[len(strn)] += 1;
    opt_len = 0;
    max = 0;
    for l in lengths:
        if lengths[l] > max:
            max = lengths[l]
            opt_len = l;
    print "insertion; length =" + str(opt_len)
    ins_profile = []
    for i in range (0, opt_len):
        ins_profile.append({})
        for j in ('A','C','T','G','N'):
            ins_profile[i][j] = 0;
    for strn in arr:
#Others are ignored
        if len(strn) == opt_len:
            for i in range (0, opt_len):
                ins_profile[i][strn[i]]+=1;
    best_ins = ''
    for i in range (0, opt_len):
        next_nucl = 'N';
        for j in ('A','C','T','G'):
            if ins_profile[i][j] > ins_profile [i][next_nucl]:
                next_nucl = j;
        best_ins += next_nucl;
    return best_ins


def process_read(arr, l,m, profile, insertions):
#    global profile
#    global insertions
    cigar = arr[5]
    aligned = arr[9];
    qual_aligned = arr[10]
    position = int(arr[3]) - 1
    map_q = int(arr[4]);
    mate = m;
    l_read = len(aligned);

    if '*' in cigar:
        return 0;


    if config["use_quality"]:
        mate *=  (1 - pow(10,map_q/-10))
    shift = 0;
    nums = []
    operations = []
    tms = "";
    aligned_length = 0;
    for i in range(0, len(cigar)):

        if cigar[i].isdigit():
             tms += cigar[i]
        else:
            nums.append(int(tms));
            operations.append(cigar[i]);
            if cigar[i]=='M':
                aligned_length += int(tms)


#	    if (cigar[i] =='D' or cigar[i] == 'I') and int(tms) > 5:
#	    	return 0;
	    tms = ''
#we do not need short reads aligned near gaps
    if aligned_length < min(l_read* 0.4, 40) and position > l_read/2 and l - position > l_read/2 and mate == 1:
        return 0;
    state_pos = 0;
    shift = 0;
    skipped = 0;
    deleted = 0;
#    if position < 150:
#        print aligned+ " "+ cigar +" " + str(position)
    insertion_string = ''
    int_A = ord('A') - 1
    for i in range(0, l_read):
    #            print aligned[i];
    #            print profile[i+position - 1]
        if i + position - skipped >= l:
#            print str(i) +" " + str(position) + " " + str(skipped)+ " " + str(l_read) + " " + cigar +" " + str(l) + " " + insertion_string + "  " + operations[state_pos]
#            print "read going out of contig"
            break;

        if shift + nums[state_pos] <= i:
            shift += nums[state_pos]
            state_pos += 1
#        if position == 698:
#            print operations[state_pos]
        if insertion_string != '' and operations[state_pos] != 'I':
#            print "inserting" + str(i) +" " + str(position) + " " + str(skipped)+ " " + str(l_read) + " " + cigar +" " + str(l) + " " + insertion_string + "  " + operations[state_pos]
            ind = i+position-skipped - 1;
            if ind not in insertions:
                insertions[ind]= []
            insertions[ind].append(insertion_string)
            insertion_string = ''
        if operations[state_pos] == 'M':
            if i +  position - skipped < l:
                t_mate = mate;
                if config["use_quality"]:
                    k = pow(10, (ord(qual_aligned[i - deleted]) - int_A ) /-10);
                    if qual_aligned[i - deleted] == 'B':
                        t_mate *= 0.2
                    else:
                        t_mate = t_mate * (1 - k/(k+1))
                profile[i+position -skipped][aligned[i - deleted]] += t_mate
        else:
            if  operations[state_pos] in ('S', 'I', 'H'):
                if operations[state_pos] == 'I':
                    if insertion_string == '':
                        profile[i+position -skipped - 1 ]['I'] += mate
                    insertion_string += (aligned[i])
                skipped += 1;
            elif operations[state_pos] == 'D':
#               print str(i) +" " + str(position) + " " + str(skipped)+ " " + str(l_read) + " " + cigar +" " + str(l) + " " + insertion_string + "  " + operations[state_pos]
#                print str(i) + " " + str(i+position - skipped) + "    deletion!"
                profile[i+position - skipped]['D'] += mate
#                skipped =1;
#                shift -= 1;
                deleted += 1;
    if insertion_string != '' and operations[state_pos] != 'I':
#        print "inserting" + str(i) +" " + str(position) + " " + str(skipped)+ " " + str(l_read) + " " + cigar +" " + str(l) + " " + insertion_string + "  " + operations[state_pos]

        ind = i+position-skipped - 1;
        if ind not in insertions:
            insertions[ind]= []
        insertions[ind].append(insertion_string)
    return 1


def split_sam(filename, tmpdir):

    inFile = open(filename)
    separate_sams ={}
    print("file "+ filename+" opened")
    read_num = 0;
    paired_read = []
    for line in inFile:
        arr = line.split('\t')
        if len(arr) > 5:
            read_num += 1
            paired_read.append(line.strip())
            if read_num == 2:
                unique = {}
                for j in range(0, 2):
                    tags = paired_read[j].split()[11:]
                    parsed_tags = {}
                    unique_fl = paired_read[j].split()[2];
                    if paired_read[j].split()[5] == "*":
                        unique_fl = ''
                    else:
                        for tag in tags:
                            if (tag.split(':')[0] == "X0" and int(tag.split(':')[2]) > 1):
                                unique_fl = '';
                                break;
                    if unique_fl != '':
                        unique[unique_fl] = 1
                for cont_name in unique :
                    if cont_name in separate_sams:
                        for j in range(0,2):
                            separate_sams[cont_name].write(paired_read[j]+ '\n')
                read_num = 0;
                paired_read = []
#            if arr[2] in separate_sams:
#                separate_sams[arr[2]].write(line)

        else:
            contig = arr[1].split(':')
            if contig[0] == 'SN':
                samfilename = tmpdir + '/' +contig[1].split('_')[1] + '.pair.sam'
#                print contig[1] + " " + samfilename;
                separate_sams[contig[1]] = open(samfilename, 'w')

    for file_name in separate_sams:
        separate_sams[file_name].close()

    return 0


def split_contigs(filename, tmpdir):
    ref_seq = read_contigs(filename);
    for contig_desc in ref_seq:
        tfilename = tmpdir + '/' + contig_desc.split('_')[1] +'.fasta'

        write_fasta([[contig_desc, ref_seq[contig_desc]]], tfilename)

    return 0


def usage():
    print >> sys.stderr, 'Corrector. Simple postprocessing tool'
    print >> sys.stderr, 'Usage: python', sys.argv[0], '[options] -1 left_reads -2 right_reads -c contigs'
    print >> sys.stderr, 'Or: python', sys.argv[0], '[options] -s sam_file -c contigs'
    print >> sys.stderr, 'Options:'
    print >> sys.stderr, '-t <int> thread number'
    print >> sys.stderr, '-o <dir_name> directory to store results'
    print >> sys.stderr, '-m <int> weight for paired reads aligned properly. By default, equal to single reads weight (=1)'
    print >> sys.stderr, '--bwa <path>  path to bwa tool. Required if bwa is not in PATH'

def run_aligner():
    global config;

    #    (contigs_name, path, suf) = fileparse(config.contigs)
    if not "contigs" in config:
        print "NEED CONTIGS TO REFINE!!!"
        usage()
        exit()
    if not "reads1" in config or not "reads2" in config:
        print "NEED READS TO REFINE!!!"
        usage()
        exit()
    contigs_name = config["contigs"].split('/')[-1];
    work_dir = config['work_dir']
    os.system("cp " + config["contigs"] + " " + work_dir)
    os.system("cp " + config["reads1"] + " " + work_dir)
    os.system("cp " + config["reads2"] + " " + work_dir)
    contigs_name = work_dir + contigs_name
    reads_1 = work_dir + config["reads1"].split('/')[-1]
    reads_2 = work_dir + config["reads2"].split('/')[-1]
    config["reads1"] = reads_1
    config["reads2"] = reads_2
    config["sam_file"] = work_dir + "tmp.sam"
    config["contigs"] = contigs_name
    print ("config")
    if "bowtie2" in config:
        print("bowtie2 found, aligning")
        run_bowtie2()
    else:
        print("running bwa")
        run_bwa()
def run_bowtie2():
    global config;
    os.system(config["bowtie2"] + "-build " + config["contigs"] + " " + config["work_dir"] + "tmp")
    os.system(config["bowtie2"] + " -x" + config["work_dir"] + "tmp  -1 " + config["reads1"] + " -2 " + config["reads2"] + " -S " + config["sam_file"] + " -p " + str(config["t"])+ " --local  --non-deterministic")
def run_bwa():
# align with BWA
    global config;

    os.system(config["bwa"] + " index -a is " + config["contigs"] + " 2")
    os.system(config["bwa"] + " aln  "+ config["contigs"] +" " + config["reads1"] + " -t " + str(config["t"]) + "  -O 7 -E 2 -k 3 -n 0.08 -q 15 >"+config["work_dir"]+ "tmp1.sai" )
    os.system(config["bwa"] + " aln  "+ config["contigs"] +" " + config["reads2"] + " -t " + str(config["t"]) + " -O 7 -E 2 -k 3 -n 0.08 -q 15 >"+config["work_dir"]+ "tmp2.sai" )
    os.system(config["bwa"] + " sampe "+ config["contigs"] +" " + config["work_dir"]+ "tmp1.sai "+config["work_dir"]+ "tmp2.sai " + config["reads1"] + " " + config["reads2"] + ">"+config["work_dir"]+ "tmp.sam 2>"+config["work_dir"]+ "isize.txt")

#    my ($sai1, $sai2, $sam) = ("reads1_aln.sai", "reads2_aln.sai", "reads_aln.sam");
#    my $cmd;
#    $cmd = "$bwa index -a is $contigs 2>/dev/null";
#    system($cmd);
#
#    my ($max_ge, $max_go, $k, $n) = (100, 3, 3, 0.08);
#    $bwa_aln1_cmd  = "$bwa aln $contigs_name $reads1 -t $num_threads -O 7 -E 2 -k $k -n $n -q 15";
#    $bwa_aln2_cmd  = "$bwa aln $contigs_name $reads2 -t $num_threads -O 7 -E 2 -k $k -n $n -q 15";
#}
#    $cmd = $bwa_aln1_cmd . " > $sai1";
#    system($cmd);
#    $cmd = $bwa_aln2_cmd . " > $sai2";
#    system($cmd);
#
#    print "\nGenerating paired-end alignments & estimating insert-size...\n";
#    my $is = "isize.txt";
#    my $bwa_sampe_cmd = "$bwa sampe $contigs $sai1 $sai2 $reads1 $reads2 > $sam 2>$is";
    return 0


def parse_profile(args):
    global config

    long_options = "threads= sam_file= output_dir= bwa= contigs= mate_weight= splitted_dir= bowtie2= help debug use_quality".split()
    short_options = "1:2:o:s:S:c:t:m:q"
    options, contigs_fpaths = getopt.gnu_getopt(args, short_options, long_options)
    for opt, arg in options:
    # Yes, this is a code duplicating. Python's getopt is non well-thought!!
        if opt in ('-o', "--output-dir"):
            config["output_dirpath"] = os.path.abspath(arg)
            config["make_latest_symlink"] = False
        if opt in ('-c', "--contigs"):
            config["contigs"] = os.path.abspath(arg)
        if opt in ('-1'):
            config["reads1"] = os.path.abspath(arg)
        if opt in ('-2'):
            config["reads2"] = os.path.abspath(arg)
        if opt in ("--bwa"):
            config["bwa"] = os.path.abspath(arg)
        if opt in ('-t', "--threads"):
            config["t"] = int(arg)
        if opt in ('-m', "--mate_weight"):
            config["mate_weight"] = float(arg)
        if opt in ('-s', "--sam_file"):
            config["sam_file"] = os.path.abspath(arg)
        if opt in ('-S', "--splitted_dir"):
            config["splitted_dir"] = os.path.abspath(arg)
        if opt in ('-q', "--use_quality"):
            config["use_quality"]= 1;
        if opt in ("--bowtie2"):
            config["bowtie2"]= os.path.abspath(arg);
    os.system ("mkdir " +config["output_dirpath"])
    os.system ("mkdir " +config["output_dirpath"] + "/tmp")
    work_dir = config["output_dirpath"]+"/tmp/"
    config["work_dir"] = work_dir


def init_config():
    now = datetime.datetime.now()
    config["output_dirpath"] = "corrector.output." + now.strftime("%Y.%m.%d_%H.%M.%S")+"/";
    config["bwa"] = "bwa"
    config["t"] = int(4)
    config["mate_weight"] = float(1)
    config["use_quality"] = 0;

def process_contig(samfilename, contig_file):

    profile = []
    insertions = {}
    inserted = 0;
    replaced = 0;
    deleted = 0;
    samFile = open(samfilename, 'r');
    fasta_contig = read_genome(contig_file);
    print "processing " + str(contig_file) + ", contig length:" + str(len(fasta_contig[1]));
    contig = fasta_contig[1].upper()
    #            profile = []
    total_reads = 0;
    indelled_reads = 0;
    #            print samfilename
    refinedFileName = config["work_dir"] + samfilename.split('/')[-1].split('.')[0] + '.ref.fasta';
    #    samFile = io.open( sys.argv[1], 'r');
    # accurate!
    cont_num = contig_file.split('/')[-1].split('.')[0];
    #    fasta_contig = read_genome(sys.argv[2]);



    l = len(contig)
    #            print l;
    #            print contig
    rescontig = ""
    for i in range (0, l):
        profile.append( {} );
        for j in ('A','C','G','T','N','I','D'):
            profile[i][j] = 0;
    insertions = {}
    #TODO: estimation on insert size, need to be replaced
    insert_size_est = 400
    for line in samFile:
        arr = line.split();
        if arr[2].split('_')[1] != cont_num:
            continue
            #        print line;
        cigar = arr[5]
        aligned = arr[9];
        position = int(arr[3]) - 1
        tags = arr[11:]
        parsed_tags = {}
        for tag in tags:
            parsed_tags[tag.split(':')[0]] = tag.split(':')[2]
        mate_el = arr[6];
        #Mate of non-end read in other contig
        #TODO: contig breaker/ fixer can be here
        if mate_el != '=' and mate_el != '*' and (position > insert_size_est and position < l - insert_size_est - 100 ):
            continue;
        #Mate not in this contig; another alignment of this read present
        if mate_el != '=' and ("XA" in parsed_tags):
#        and (("X0" in parsed_tags and parsed_tags["X0"] > 1)):

        #                    if abs(position - 200) < 100:
        #                        print position + 1
        #                        print "XA tag present, read: " + arr[0] +" cigar " + cigar
            continue;
        if mate_el == '=' and (int(arr[1]) & 8) == 0:
            mate = config["mate_weight"];
        else:
            mate = 1;
        indelled_reads += 1 - process_read(arr, l, mate, profile, insertions)
        total_reads += 1;

    #            if len(insertions) < 50:
    #                print insertions
    #            else:
    #                print "insertions very big, most popular:"
    #                for element in insertions:
    #                    if len(insertions[element]) > 20 or profile[int(element)]['I'] * 3 > profile[int(element)][contig[int(element)]] :
    #                        print str(element) + str(insertions[element])
    #                        print "profile here: " + str(profile[element]) """
    for i in range (0, l):
    #*
    #                if i in range(183, 224):
    #                    print "FFFFFFUUUU "+ str(i + 1)
    #                    print(profile[i])

        tj = contig[i]
        tmp =''
        for count in range(0,2):
            #exluded 'N's from cycle
            for j in ('A','C','G','T','N', 'I', 'D'):
                if profile[i][tj] < profile[i][j] or (j == 'I' and profile[i][tj] < 1.5 * profile[i][j] and profile[i][j] > 2):
                    tj = j
                    #                rescontig[i] = j
            if tj != contig[i] :
                if tj =='I' or tj == 'D' :
                    print "there was in-del"
                else:
                    replaced += 1
                print profile[i]
                print "changing " + contig[i] + " to " + tj + " on position " + str(i+1) + '\n'

            if tj in ('A','C','T','G','N'):
                rescontig += tj
                break;
            elif tj == 'D':
                print "skipping deletion"+ " on position " + str(i+1) + '\n'
                deleted += 1;
                break;
            elif tj == 'I':
                tmp = vote_insertions(i,insertions);

                profile[i]['I'] = 0;
        if tmp != '':
            rescontig += tmp;
            inserted += len (tmp)

            #            print(fasta_contig[0]);
    res_fasta = [[]]
    #res_fasta.append([])
    res_fasta[0].append(fasta_contig[0]);
    res_fasta[0].append(rescontig)
    write_fasta(res_fasta, refinedFileName)
    #    refinedFile.write(rescontig);
    #           nonFasta.write(rescontig);

    print "Finished processing "+ str(contig_file) + ". Used " + str(total_reads) + " reads."
    print "replaced: " + str(replaced) + " deleted: "+ str(deleted) +" inserted: " + str(inserted)
#    return inserted, replaced


def main(args):

    if len(args) < 1:
        usage()
        exit(0)

    deleted = 0;
    init_config()
    parse_profile(args)
    inserted = 0;
    replaced = 0;
    print(config)
    if "splitted_dir" not in config:
#        print "no splitted dir, looking for sam file"
        if "sam_file" not in config:
            print "no sam file, running aligner"
            run_aligner()
        else:
            print "sam file found"
            os.system("cp "+ config["sam_file"] +" " + config["work_dir"]+"tmp.sam")
            config["sam_file"] = config["work_dir"]+"tmp.sam"
    #    now = datetime.datetime.now()
    #    res_directory = "corrector.output." + now.strftime("%Y.%m.%d_%H.%M.%S")+"/";
        split_contigs(config["contigs"],config["work_dir"])
        print("contigs splitted, starting splitting .sam file");
        split_sam(config["sam_file"], config["work_dir"])
        print(".sam file splitted")
    else:
        print "splitted tmp dir found, starting voting"
        os.system("cp "+ config["splitted_dir"] +"/* " + config["work_dir"])
    #    return 0
#    if not os.path.exists(res_directory):
#        os.makedirs(res_directory)
#    refinedFileName = res_directory + sys.argv[2].split('/')[-1].split('.')[0] + '.ref.fasta';

    filelist = [os.path.abspath(os.path.join(config["work_dir"], i)) for i in os.listdir(config["work_dir"]) if os.path.isfile(os.path.join(config["work_dir"], i))]
    pairs = []
    for contig_file in filelist:
        f_name = contig_file.split('/')[-1];
        f_arr = f_name.split('.');
        if len(f_arr) == 2 and f_arr[1][0:2] == "fa" and os.path.exists(os.path.join("/".join(contig_file.split('/')[:-1]), f_arr[0]+".pair.sam")):

            samfilename = os.path.join("/".join(contig_file.split('/')[:-1]), f_arr[0]+".pair.sam");
            tmp = []
            tmp.append(samfilename)
            tmp.append(contig_file)
            pairs.append(tmp)
    print pairs[0];
#    Parallel(n_jobs=config["t"])(delayed(process_contig)(pair[0],pair[1])for pair in pairs)
    for pair in pairs:
        (loc_ins, loc_rep) = process_contig(pair[0], pair[1])
        inserted += loc_ins;
        replaced += loc_rep;

    cat_line = "cat "+ config["work_dir"] + "/*.ref.fasta > "+ config["work_dir"] + "../corrected_contigs.fasta"
    print cat_line
    os.system(cat_line);
#    print "TOTAL replaced: "+ str(replaced) + " inserted: "+ str(inserted) + " deleted: " + str(deleted);


if __name__ == '__main__':
    main(sys.argv[1:])