#!/usr/bin/env python

############################################################################
# Copyright (c) 2011-2013 Saint-Petersburg Academic University
# All Rights Reserved
# See file LICENSE for details.
############################################################################

import os
import shutil
import sys
import getopt
import logging
import platform

import spades_init

spades_init.init()
spades_home = spades_init.spades_home
bin_home = spades_init.bin_home
python_modules_home = spades_init.python_modules_home
ext_python_modules_home = spades_init.ext_python_modules_home
spades_version = spades_init.spades_version

import support
from process_cfg import *
import bh_logic
import spades_logic


def print_used_values(cfg, log):
    def print_value(cfg, section, param, pretty_param="", margin="  "):
        if not pretty_param:
            pretty_param = param.capitalize().replace('_', ' ')
        line = margin + pretty_param
        if param in cfg[section].__dict__:
            line += ": " + str(cfg[section].__dict__[param])
        else:
            if param.find("offset") != -1:
                line += " will be auto-detected"
        log.info(line)

    log.info("")

    # system info
    log.info("System information:")
    try:
        log.info("  SPAdes version: " + str(spades_version))
        log.info("  Python version: " + str(sys.version_info[0]) + "." + str(sys.version_info[1]) + '.' + str(sys.version_info[2]))
        # for more details: '[' + str(sys.version_info) + ']'
        log.info("  OS: " + platform.platform())
        # for more deatils: '[' + str(platform.uname()) + ']'
    except:
        log.info("  Problem occurred when getting system information")
    log.info("")

    # main
    print_value(cfg, "common", "output_dir", "", "")
    if ("error_correction" in cfg) and (not "assembly" in cfg):
        log.info("Mode: ONLY read error correction (without assembling)")
    elif (not "error_correction" in cfg) and ("assembly" in cfg):
        log.info("Mode: ONLY assembling (without read error correction)")
    else:
        log.info("Mode: read error correction and assembling")
    if ("common" in cfg) and ("developer_mode" in cfg["common"].__dict__):
        if cfg["common"].developer_mode:
            log.info("Debug mode turned ON")
        else:
            log.info("Debug mode turned OFF")
    log.info("")

    # dataset
    if "dataset" in cfg:
        log.info("Dataset parameters:")

        if cfg["dataset"].single_cell:
            log.info("  Single-cell mode")
        else:
            log.info("  Multi-cell mode (you should set '--sc' flag if input data"\
                     " was obtained with MDA (single-cell) technology")

        no_single = True
        no_paired = True
        for k, v in cfg["dataset"].__dict__.items():
            if k.startswith("single_reads") or k.startswith("paired_reads"):
                if k.startswith("paired_reads"):
                    no_paired = False
                    if not isinstance(v, list):
                        log.info("  Paired reads (file with interlaced reads):")
                    else:
                        log.info("  Paired reads (files with left and right reads):")
                else:
                    no_single = False
                    log.info("  Single reads:")
                if not isinstance(v, list):
                    v = [v]
                for reads_file in v:
                    log.info("    " + os.path.abspath(os.path.expandvars(reads_file)))

        if no_paired:
            log.info("  Paired reads were not specified")
        if no_single:
            log.info("  Single reads were not specified")

    # error correction
    if "error_correction" in cfg:
        log.info("Read error correction parameters:")
        print_value(cfg, "error_correction", "tmp_dir", "Dir for temp files")
        print_value(cfg, "error_correction", "max_iterations", "Iterations")
        print_value(cfg, "error_correction", "qvoffset", "PHRED offset")

        if cfg["error_correction"].gzip_output:
            log.info("  Corrected reads will be compressed (with gzip)")
        else:
            log.info("  Corrected reads will NOT be compressed (with gzip)")


    # assembly
    if "assembly" in cfg:
        log.info("Assembly parameters:")
        print_value(cfg, "assembly", "iterative_K", "k")

        if cfg["assembly"].gap_closer:
            log.info("  The gap closer will be used")
        else:
            log.info("  The gap closer will NOT be used")

    log.info("Other parameters:")
    print_value(cfg, "common", "max_threads", "Threads")
    print_value(cfg, "common", "max_memory", "Memory limit (in Gb)", "  ")
    log.info("")


def set_defaults(cfg):
    ## setting default values in config

    # common
    if not "output_dir" in cfg["common"].__dict__:
        cfg["common"].__dict__["output_dir"] = 'spades_output'

    if not "max_threads" in cfg["common"].__dict__:
        cfg["common"].__dict__["max_threads"] = 16

    if not "max_memory" in cfg["common"].__dict__:
        cfg["common"].__dict__["max_memory"] = 250

    if not "output_to_console" in cfg["common"].__dict__:
        cfg["common"].__dict__["output_to_console"] = True

    if not "developer_mode" in cfg["common"].__dict__:
        cfg["common"].__dict__["developer_mode"] = False

    # dataset
    if "dataset" in cfg:
        if not "single_cell" in cfg["dataset"].__dict__:
            cfg["dataset"].__dict__["single_cell"] = False

    # error_correction
    if "error_correction" in cfg:
        if not "max_iterations" in cfg["error_correction"].__dict__:
            cfg["error_correction"].__dict__["max_iterations"] = 1
        if not "gzip_output" in cfg["error_correction"].__dict__:
            cfg["error_correction"].__dict__["gzip_output"] = True

    # assembly
    if "assembly" in cfg:
        if not "iterative_K" in cfg["assembly"].__dict__:
            cfg["assembly"].__dict__["iterative_K"] = [21, 33, 55]
        if not "gap_closer" in cfg["assembly"].__dict__:
            cfg["assembly"].__dict__["gap_closer"] = True


def check_config(cfg, log):
    ## special case: 0 iterations for the error correction means "No error correction!"

    if ("error_correction" in cfg) and ("max_iterations" in cfg["error_correction"].__dict__):
        if cfg["error_correction"].max_iterations == 0:
            del cfg["error_correction"]

    ## checking mandatory sections

    if (not "dataset" in cfg):
        support.error("wrong config! You should specify 'dataset' section!", log)
        return False

    if (not "error_correction" in cfg) and (not "assembly" in cfg):
        support.error("wrong options! You should specify either '--only-error-correction' (for read"
                      " error correction) or '--only-assembler' (for assembling) or none of these options (for both)!", log)
        return False

    ## checking existence of all files in dataset section

    no_files_with_reads = True
    for k, v in cfg["dataset"].__dict__.items():
        if k.startswith("single_reads") or k.startswith("paired_reads"):
            no_files_with_reads = False
            if not isinstance(v, list):
                v = [v]
            for reads_file in v:
                if not os.path.isfile(os.path.expandvars(reads_file)):
                    support.error("file with reads doesn't exist! " + os.path.expandvars(reads_file), log)
                    return False
                else:
                    ext = os.path.splitext(os.path.expandvars(reads_file))[1]
                    if ext not in ['.fa', '.fasta', '.fq', '.fastq', '.gz']:
                        support.error("file with reads has unsupported format (only .fa, .fasta, .fq,"
                                      " .fastq, .gz are supported)! " + os.path.expandvars(reads_file), log)
                        return False

    if no_files_with_reads:
        support.error("wrong options! You should specify at least one file with reads!", log)
        return False

    # expanding vars and setting defaults depended on other settings
    cfg["common"].output_dir = os.path.abspath(os.path.expandvars(cfg["common"].output_dir))

    if "error_correction" in cfg:
        cfg["error_correction"].__dict__["output_dir"] = os.path.join(
            cfg["common"].output_dir, "corrected")
        if not "tmp_dir" in cfg["error_correction"].__dict__:
            cfg["error_correction"].__dict__["tmp_dir"] = cfg["error_correction"].output_dir
        cfg["error_correction"].tmp_dir = os.path.join(os.path.abspath(
            os.path.expandvars(cfg["error_correction"].tmp_dir)), 'tmp')

    return True


def check_binaries(binary_dir, log):
    for binary in ["hammer", "spades", "bwa-spades"]:
        binary_path = os.path.join(binary_dir, binary)
        if not os.path.isfile(binary_path):
            support.error("SPAdes binaries cannot found: " + binary_path +
                          "\nYou can obtain SPAdes binaries in one of two ways:" +
                          "\n1. Download SPAdes binaries from http://spades.bioinf.spbau.ru/release2.4.0/SPAdes-2.4.0-Linux.tar.gz" +
                          "\n2. Build source code with ./spades_compile.sh script", log)
            return False
    return True


# for spades.py --test
def fill_test_config(cfg):
    # common
    cfg["common"] = load_config_from_vars(dict())
    cfg["common"].__dict__["output_dir"] = 'spades_test'

    # dataset
    cfg["dataset"] = load_config_from_vars(dict())
    cfg["dataset"].__dict__["paired_reads"] =\
    [os.path.join(spades_home, "test_dataset/ecoli_1K_1.fq.gz"),
     os.path.join(spades_home, "test_dataset/ecoli_1K_2.fq.gz")]
    cfg["dataset"].__dict__["single_cell"] = False

    # error_correction (empty config, i.e. load default params)
    cfg["error_correction"] = load_config_from_vars(dict())

    # assembly (empty config, i.e. load default params)
    cfg["assembly"] = load_config_from_vars(dict())

    # loading default parameters
    set_defaults(cfg)


long_options = "12= threads= memory= tmp-dir= iterations= phred-offset= sc "\
               "only-error-correction only-assembler "\
               "disable-gzip-output help test debug reference= "\
               "bh-heap-check= spades-heap-check= help-hidden "\
               "config-file= dataset= mismatch-correction careful rectangles".split()
short_options = "o:1:2:s:k:t:m:i:h"


def check_file(f, message, log):
    if not os.path.isfile(f):
        support.error("file not found (%s): %s" % (message, f), log)
    return f


def check_files_duplication(files, log):
    for file in files:
        if files.count(file) != 1:
            support.error("file %s was specified at least twice" % file, log)


def usage(show_hidden=False):
    print >> sys.stderr, "SPAdes genome assembler v." + str(spades_version)
    print >> sys.stderr, "Usage:", sys.argv[0], "[options] -o <output_dir>"
    print >> sys.stderr, ""
    print >> sys.stderr, "Basic options:"
    print >> sys.stderr, "-o\t<output_dir>\tdirectory to store all the resulting files (required)"
    print >> sys.stderr, "--sc\t\t\tthis flag is required for MDA (single-cell)"\
                         " data"
    print >> sys.stderr, "--12\t<filename>\tfile with interlaced forward and reverse"\
                         " paired-end reads"
    print >> sys.stderr, "-1\t<filename>\tfile with forward paired-end reads"
    print >> sys.stderr, "-2\t<filename>\tfile with reverse paired-end reads"
    print >> sys.stderr, "-s\t<filename>\tfile with unpaired reads"
    print >> sys.stderr, "--test\t\t\truns SPAdes on toy dataset"
    print >> sys.stderr, "-h/--help\t\tprints this usage message"

    print >> sys.stderr, ""
    print >> sys.stderr, "Pipeline options:"
    print >> sys.stderr, "--only-error-correction\truns only read error correction"\
                         " (without assembling)"
    print >> sys.stderr, "--only-assembler\truns only assembling (without read error"\
                         " correction)"
    print >> sys.stderr, "--disable-gzip-output\tforces error correction not to"\
                         " compress the corrected reads"
    print >> sys.stderr, "--careful\t\ttries to reduce number"\
                         " of mismatches and short indels"
    print >> sys.stderr, "--rectangles\t\tuses rectangle graph algorithm for repeat resolution"

    print >> sys.stderr, ""
    print >> sys.stderr, "Advanced options:"
    print >> sys.stderr, "-t/--threads\t<int>\t\tnumber of threads"
    print >> sys.stderr, "\t\t\t\t[default: 16]"
    print >> sys.stderr, "-m/--memory\t<int>\t\tRAM limit for SPAdes in Gb"\
                         " (terminates if exceeded)"
    print >> sys.stderr, "\t\t\t\t[default: 250]"
    print >> sys.stderr, "--tmp-dir\t<dirname>\tdirectory for read error correction"\
                         " temporary files"
    print >> sys.stderr, "\t\t\t\t[default: <output_dir>/corrected/tmp]"
    print >> sys.stderr, "-k\t\t<int,int,...>\tcomma-separated list of k-mer sizes"\
                         " (must be odd and"
    print >> sys.stderr, "\t\t\t\tless than 128) [default: 21,33,55]"
    print >> sys.stderr, "-i/--iterations\t<int>\t\tnumber of iterations for read error"\
                         " correction"
    print >> sys.stderr, "\t\t\t\t[default: 1]"
    print >> sys.stderr, "--phred-offset\t<33 or 64>\tPHRED quality offset in the"\
                         " input reads (33 or 64)"
    print >> sys.stderr, "\t\t\t\t[default: auto-detect]"
    print >> sys.stderr, "--debug\t\t\t\truns SPAdes in debug mode (keeps intermediate output)"

    if show_hidden:
        print >> sys.stderr, ""
        print >> sys.stderr, "HIDDEN options:"
        print >> sys.stderr, "--mismatch-correction\t\truns post processing correction"\
                             " of mismatches and short indels"
        print >> sys.stderr, "--config-file\t<filename>\tconfiguration file for spades.py"\
                             " (WARN: all other options will be skipped)"
        print >> sys.stderr, "--dataset\t<filename>\tfile with dataset description"\
                             " (WARN: works exclusively in --only-assembler mode)"
        print >> sys.stderr, "--reference\t<filename>\tfile with reference for deep analysis"\
                             " (only in debug mode)"
        print >> sys.stderr, "--bh-heap-check\t\t<value>\tsets HEAPCHECK environment variable"\
                             " for BayesHammer"
        print >> sys.stderr, "--spades-heap-check\t<value>\tsets HEAPCHECK environment variable"\
                             " for SPAdes"
        print >> sys.stderr, "--help-hidden\tprints this usage message with all hidden options"    


def main():
    os.environ["LC_ALL"] = "C"

    CONFIG_FILE = ""
    options = None
    TEST = False

    if len(sys.argv) == 1:
        usage()
        sys.exit(0)

    try:
        options, not_options = getopt.gnu_getopt(sys.argv, short_options, long_options)
    except getopt.GetoptError, err:
        print >> sys.stderr, err
        print >> sys.stderr
        usage()
        sys.exit(1)

    log = logging.getLogger('spades')
    log.setLevel(logging.DEBUG)

    console = logging.StreamHandler(sys.stdout)
    console.setFormatter(logging.Formatter('%(message)s'))
    console.setLevel(logging.DEBUG)
    log.addHandler(console)

    # all parameters are stored here
    cfg = dict()

    if options:
        output_dir = ""
        tmp_dir = ""
        reference = ""
        dataset = ""

        paired = []
        paired1 = []
        paired2 = []
        single = []
        k_mers = []

        single_cell = False
        disable_gzip_output = False

        only_error_correction = False
        only_assembler = False

        bh_heap_check = ""
        spades_heap_check = ""

        developer_mode = False

        threads = None
        memory = None
        qvoffset = None
        iterations = None

        rectangles = None
        #corrector
        mismatch_corrector = False
        bwa = os.path.join(bin_home, "bwa-spades") # it is hardcoded now instead of option
        careful = False

        for opt, arg in options:
            if opt == '-o':
                output_dir = arg
            elif opt == "--tmp-dir":
                tmp_dir = arg
            elif opt == "--reference":
                reference = check_file(arg, 'reference', log)
            elif opt == "--dataset":
                dataset = check_file(arg, 'dataset', log)

            elif opt == "--12":
                paired.append(check_file(arg, 'paired', log))
            elif opt == '-1':
                paired1.append(check_file(arg, 'left paired', log))
            elif opt == '-2':
                paired2.append(check_file(arg, 'right paired', log))
            elif opt == '-s':
                single.append(check_file(arg, 'single', log))
            elif opt == '-k':
                k_mers = map(int, arg.split(","))
                for k in k_mers:
                    if k > 127:
                        support.error('wrong k value ' + str(k) + ': all k values should be less than 128', log)
                    if k % 2 == 0:
                        support.error('wrong k value ' + str(k) + ': all k values should be odd', log)

            elif opt == "--sc":
                single_cell = True
            elif opt == "--disable-gzip-output":
                disable_gzip_output = True

            elif opt == "--only-error-correction":
                only_error_correction = True
            elif opt == "--only-assembler":
                only_assembler = True

            elif opt == "--bh-heap-check":
                bh_heap_check = arg
            elif opt == "--spades-heap-check":
                spades_heap_check = arg

            elif opt == '-t' or opt == "--threads":
                threads = int(arg)
            elif opt == '-m' or opt == "--memory":
                memory = int(arg)
            elif opt == "--phred-offset":
                qvoffset = int(arg)
            elif opt == '-i' or opt == "--iterations":
                iterations = int(arg)

            elif opt == "--debug":
                developer_mode = True

            elif opt == "--rectangles":
                rectangles = True

            #corrector
            elif opt == "--mismatch-correction":
                mismatch_corrector = True

            elif opt == "--careful":
                mismatch_corrector = True
                careful = True
            
            elif opt == '-h' or opt == "--help":
                usage()
                sys.exit(0)
            elif opt == "--help-hidden":
                usage(True)
                sys.exit(0)

            elif opt == "--config-file":
                CONFIG_FILE = check_file(arg, 'config file', log)
                break

            elif opt == "--test":
                fill_test_config(cfg)
                TEST = True
                break
            else:
                raise ValueError

        if not CONFIG_FILE and not TEST:
            if not output_dir:
                support.error("the output_dir is not set! It is a mandatory parameter (-o output_dir).", log)

            if len(paired1) != len(paired2):
                support.error("the number of files with left paired reads is not equal to the"
                      " number of files with right paired reads!", log)

            # processing hidden option "--dataset"
            if dataset:
                if not only_assembler:
                    support.error("hidden option --dataset works exclusively in --only-assembler mode!", log)
                # reading info about dataset from provided dataset file
                cfg["dataset"] = load_config_from_file(dataset)
                # correcting reads relative pathes (reads can be specified relatively to provided dataset file)
                for k, v in cfg["dataset"].__dict__.iteritems():
                    if k.find("_reads") != -1:
                        corrected_reads_filenames = []
                        if not isinstance(v, list):
                            v = [v]
                        for reads_filename in v:
                            corrected_reads_filename = os.path.join(os.path.dirname(dataset), reads_filename)
                            check_file(corrected_reads_filename, k + " in " + dataset, log)
                            corrected_reads_filenames.append(corrected_reads_filename)
                        cfg["dataset"].__dict__[k] = corrected_reads_filenames
            else:
                if not paired and not paired1 and not single:
                    support.error("you should specify either paired reads (-1, -2 or -12) or single reads (-s) or both!", log)
                check_files_duplication(paired + paired1 + paired2 + single, log)

                # creating empty "dataset" section
                cfg["dataset"] = load_config_from_vars(dict())

                # filling reads
                paired_counter = 0
                if paired:
                    for read in paired:
                        paired_counter += 1
                        cfg["dataset"].__dict__["paired_reads#" + str(paired_counter)] = read

                if paired1:
                    for i in range(len(paired1)):
                        paired_counter += 1
                        cfg["dataset"].__dict__["paired_reads#" + str(paired_counter)] = [paired1[i],
                                                                                          paired2[i]]
                        #cfg["dataset"].__dict__["paired_reads"] = [paired1[i], paired2[i]]

                if single:
                    cfg["dataset"].__dict__["single_reads"] = single

                # filling other parameters
                cfg["dataset"].__dict__["single_cell"] = single_cell
                if developer_mode and reference:
                    cfg["dataset"].__dict__["reference"] = reference

            # filling cfg
            cfg["common"] = load_config_from_vars(dict())
            if not only_assembler:
                cfg["error_correction"] = load_config_from_vars(dict())
            if not only_error_correction:
                cfg["assembly"] = load_config_from_vars(dict())
            set_defaults(cfg)

            # common
            if output_dir:
                cfg["common"].__dict__["output_dir"] = output_dir
            if threads:
                cfg["common"].__dict__["max_threads"] = threads
            if memory:
                cfg["common"].__dict__["max_memory"] = memory
            if developer_mode:
                cfg["common"].__dict__["developer_mode"] = developer_mode
            if mismatch_corrector:
                cfg["common"].__dict__["mismatch-correction"] = mismatch_corrector

            # error correction
            if not only_assembler:
                if tmp_dir:
                    cfg["error_correction"].__dict__["tmp_dir"] = tmp_dir
                if qvoffset:
                    cfg["error_correction"].__dict__["qvoffset"] = qvoffset
                if iterations != None:
                    cfg["error_correction"].__dict__["max_iterations"] = iterations
                if bh_heap_check:
                    cfg["error_correction"].__dict__["heap_check"] = bh_heap_check
                cfg["error_correction"].__dict__["gzip_output"] = not disable_gzip_output

            # assembly
            if not only_error_correction:
                if k_mers:
                    cfg["assembly"].__dict__["iterative_K"] = k_mers
                if spades_heap_check:
                    cfg["assembly"].__dict__["heap_check"] = spades_heap_check
                cfg["assembly"].__dict__["careful"] = careful

            #corrector can work only if contigs are exists (not only error correction)
            if (paired1 or paired) and (not only_error_correction) and mismatch_corrector:
                cfg["mismatch_corrector"] = load_config_from_vars(dict())
                cfg["mismatch_corrector"].__dict__["skip-masked"] = ""
                if bwa:
                    cfg["mismatch_corrector"].__dict__["bwa"] = bwa
                if "max_threads" in cfg["common"].__dict__:
                    cfg["mismatch_corrector"].__dict__["threads"] = cfg["common"].max_threads
                if "output_dir" in cfg["common"].__dict__:
                    cfg["mismatch_corrector"].__dict__["output-dir"] = cfg["common"].output_dir
                # reads
                if paired1:
                    cfg["mismatch_corrector"].__dict__["1"] = paired1
                    cfg["mismatch_corrector"].__dict__["2"] = paired2
                if paired:
                    cfg["mismatch_corrector"].__dict__["12"] = paired

    if CONFIG_FILE:
        cfg = load_config_from_info_file(CONFIG_FILE)
        os.environ["cfg"] = os.path.dirname(os.path.abspath(CONFIG_FILE))
        set_defaults(cfg)

    if not CONFIG_FILE and not options and not TEST:
        usage()
        sys.exit(1)

    if not check_config(cfg, log):
        return

    if not check_binaries(bin_home, log):
        return

    if not os.path.isdir(cfg["common"].output_dir):
        os.makedirs(cfg["common"].output_dir)

    log_filename = os.path.join(cfg["common"].output_dir, "spades.log")
    log_handler = logging.FileHandler(log_filename, mode='w')
    log.addHandler(log_handler)

    params_filename = os.path.join(cfg["common"].output_dir, "params.txt")
    params_handler = logging.FileHandler(params_filename, mode='w')
    log.addHandler(params_handler)

    if CONFIG_FILE:
        log.info("Using config file: " + CONFIG_FILE)
    else:
        command = "Command line:"
        for v in sys.argv:
            command += " " + v
        log.info(command)

    print_used_values(cfg, log)

    log.removeHandler(params_handler)

    log.info("\n======= SPAdes pipeline started. Log can be found here: " + log_filename + "\n")

    try:
        # copying configs before all computations (to prevent its changing between Hammer and SPAdes or during SPAdes iterations)
        tmp_configs_dir = os.path.join(cfg["common"].output_dir, "configs")
        if os.path.isdir(tmp_configs_dir):
            shutil.rmtree(tmp_configs_dir)
        shutil.copytree(os.path.join(spades_home, "configs"), tmp_configs_dir)

        # creating YAML with input reads
        dataset_yaml_filename = os.path.join(cfg["common"].output_dir, "input_dataset.yaml")
        dataset_yaml = dict()
        dataset_yaml["left reads"] = []
        dataset_yaml["right reads"] = []
        dataset_yaml["single reads"] = []

        import bh_aux
        paired_end = False
        for k, v in cfg["dataset"].__dict__.items():
            if not isinstance(v, list):
                v = [v]

            if k.startswith("single_reads"):
                for item in v:
                    dataset_yaml["single reads"].append(os.path.abspath(os.path.expandvars(item)))

            elif k.startswith("paired_reads"):
                paired_end = True
                if len(v) == 1:
                    item = os.path.abspath(os.path.expandvars(v[0]))
                    if "error_correction" in cfg:
                        split_paired_reads = bh_aux.split_paired_file(item, cfg["error_correction"].tmp_dir, log)
                    else:
                        support.error("interlaced reads are not supported yet in YAML!", log)
                else: # len(v) == 2
                    split_paired_reads = v
                dataset_yaml["left reads"].append(os.path.abspath(split_paired_reads[0]))
                dataset_yaml["right reads"].append(os.path.abspath(split_paired_reads[1]))

        if paired_end:
            dataset_yaml["type"] = "paired-end"
        else:
            dataset_yaml["type"] = "single"
        dataset_yaml["orientation"] = "fr" # TODO: only in paired-end mode
        support.save_to_yaml(dataset_yaml, dataset_yaml_filename)

        bh_dataset_filename = ""
        if "error_correction" in cfg:
            bh_cfg = merge_configs(cfg["error_correction"], cfg["common"])

            if "HEAPCHECK" in os.environ:
                del os.environ["HEAPCHECK"]
            if "heap_check" in bh_cfg.__dict__:
                os.environ["HEAPCHECK"] = bh_cfg.heap_check

            bh_cfg.__dict__["dataset"] = os.path.join(bh_cfg.output_dir,
                "dataset.info")

            if os.path.exists(bh_cfg.output_dir):
                shutil.rmtree(bh_cfg.output_dir)

            os.makedirs(bh_cfg.output_dir)
            if not os.path.exists(bh_cfg.tmp_dir):
                os.makedirs(bh_cfg.tmp_dir)

            log.info("\n===== Read error correction started. \n")

            if CONFIG_FILE:
                shutil.copy(CONFIG_FILE, bh_cfg.output_dir)

            ### parsing dataset section
            bh_cfg.__dict__["single_cell"] = cfg["dataset"].single_cell
            # saving reference to dataset in developer_mode
            if bh_cfg.developer_mode:
                if "reference" in cfg["dataset"].__dict__:
                    bh_cfg.__dict__["reference_genome"] = os.path.abspath(
                        os.path.expandvars(cfg["dataset"].reference))

            bh_cfg.__dict__["dataset_yaml_filename"] = dataset_yaml_filename

            bh_dataset_filename = bh_logic.run_bh(tmp_configs_dir, bin_home, bh_cfg, log)

            log.info("\n===== Read error correction finished. \n")

        result_contigs_filename = ""
        result_scaffolds_filename = ""
        misc_dir = os.path.join(cfg["common"].output_dir, "misc")
        if "assembly" in cfg:
            spades_cfg = merge_configs(cfg["assembly"], cfg["common"])

            if "HEAPCHECK" in os.environ:
                del os.environ["HEAPCHECK"]
            if "heap_check" in spades_cfg.__dict__:
                os.environ["HEAPCHECK"] = spades_cfg.heap_check

            if "error_correction" in cfg:
                spades_cfg.__dict__["align_original_reads"] = True
            else:
                spades_cfg.__dict__["align_original_reads"] = False

            has_paired = False
            for k in cfg["dataset"].__dict__.keys():
                if k.startswith("paired_reads"):
                    has_paired = True
                    break
            if has_paired:
                spades_cfg.__dict__["paired_mode"] = True
            else:
                spades_cfg.__dict__["paired_mode"] = False

            spades_cfg.__dict__["result_contigs"] = os.path.join(spades_cfg.output_dir,
                "contigs.fasta")
            spades_cfg.__dict__["result_scaffolds"] = os.path.join(spades_cfg.output_dir,
                "scaffolds.fasta")

            spades_cfg.__dict__["additional_contigs"] = os.path.join(spades_cfg.output_dir,
                "simplified_contigs.fasta")

            if rectangles:
                spades_cfg.__dict__["resolving_mode"] = "rectangles"

            log.info("\n===== Assembling started.\n")

            if CONFIG_FILE:
                shutil.copy(CONFIG_FILE, spades_cfg.output_dir)

            # dataset created during error correction
            if bh_dataset_filename:
                spades_cfg.__dict__["dataset"] = bh_dataset_filename

            if not "dataset" in spades_cfg.__dict__:
                # creating dataset
                dataset_filename = os.path.join(spades_cfg.output_dir, "dataset.info")
                dataset_file = open(dataset_filename, 'w')
                import process_cfg
                dataset_file.write("single_cell" + '\t' + process_cfg.bool_to_str(cfg["dataset"].single_cell) + '\n')
                dataset_file.write("reads" + '\t' + dataset_yaml_filename + '\n')

                # saving reference to dataset in developer_mode
                if spades_cfg.developer_mode:
                    if "reference" in cfg["dataset"].__dict__:
                        dataset_file.write("reference_genome" + '\t')
                        dataset_file.write(os.path.abspath(
                            os.path.expandvars(cfg["dataset"].reference)) + '\n')

                dataset_file.close()
                spades_cfg.__dict__["dataset"] = dataset_filename

            result_contigs_filename, result_scaffolds_filename, latest_dir = spades_logic.run_spades(tmp_configs_dir,
                bin_home, spades_cfg, log)

            #RECTANGLES
            if spades_cfg.paired_mode and rectangles:
                sys.path.append(os.path.join(python_modules_home, "rectangles"))
                import rrr

                rrr_input_dir = os.path.join(latest_dir, "saves")
                rrr_outpath = os.path.join(spades_cfg.output_dir, "rectangles")
                if not os.path.exists(rrr_outpath):
                    os.mkdir(rrr_outpath)

                rrr_reference_information_file = os.path.join(rrr_input_dir,
                    "late_pair_info_counted_etalon_distance.txt")
                rrr_test_util = rrr.TestUtils(rrr_reference_information_file,
                    os.path.join(rrr_outpath, "rectangles.log"))
                rrr.resolve(rrr_input_dir, rrr_outpath, rrr_test_util, "", cfg["dataset"].single_cell, spades_cfg.careful)

                shutil.copyfile(os.path.join(rrr_outpath, "rectangles_extend_before_scaffold.fasta"), spades_cfg.result_contigs)
                shutil.copyfile(os.path.join(rrr_outpath, "rectangles_extend.fasta"), spades_cfg.result_scaffolds)

                if not spades_cfg.developer_mode:
                    if os.path.exists(rrr_input_dir):
                        shutil.rmtree(rrr_input_dir)
                    if os.path.exists(rrr_outpath):
                        shutil.rmtree(rrr_outpath, True)
                    if os.path.exists(rrr_outpath):
                        os.system('rm -r ' + rrr_outpath)
                        #EOR

            if os.path.isdir(misc_dir):
                shutil.rmtree(misc_dir)
            os.makedirs(misc_dir)
            if os.path.isfile(spades_cfg.additional_contigs):
                shutil.move(spades_cfg.additional_contigs, misc_dir)

            log.info("\n===== Assembling finished. \n")

        #corrector
        if "mismatch_corrector" in cfg and os.path.isfile(result_contigs_filename):
            to_correct = dict()
            to_correct["contigs"] = (result_contigs_filename, os.path.join(misc_dir, "assembled_contigs.fasta"))
            if os.path.isfile(result_scaffolds_filename):
                to_correct["scaffolds"] = (result_scaffolds_filename, os.path.join(misc_dir, "assembled_scaffolds.fasta"))

            log.info("\n===== Mismatch correction started.")

            # moving assembled contigs (scaffolds) to misc dir
            for k, (old, new) in to_correct.items():
                shutil.move(old, new)

            import corrector
            corrector_cfg = cfg["mismatch_corrector"]
            args = []
            for key, values in corrector_cfg.__dict__.items():
                if key == "output-dir":
                    continue

                # for processing list of reads
                if not isinstance(values, list):
                    values = [values]
                for value in values:
                    if len(key) == 1:
                        args.append('-' + key)
                    else:
                        args.append('--' + key)
                    if value:
                        args.append(value)

            # processing contigs and scaffolds (or only contigs)
            for k, (corrected, assembled) in to_correct.items():
                log.info("\n== Processing " + k + "\n")

                cur_args = args[:]
                cur_args += ['-c', assembled]
                tmp_dir_for_corrector = os.path.join(corrector_cfg.__dict__["output-dir"], "mismatch_corrector_" + k)
                cur_args += ['--output-dir', tmp_dir_for_corrector]

                # correcting
                corrector.main(cur_args, ext_python_modules_home, log)

                result_corrected_filename = os.path.abspath(os.path.join(tmp_dir_for_corrector, "corrected_contigs.fasta"))
                # moving corrected contigs (scaffolds) to SPAdes output dir
                if os.path.isfile(result_corrected_filename):
                    shutil.move(result_corrected_filename, corrected)

                if os.path.isdir(tmp_dir_for_corrector):
                    shutil.rmtree(tmp_dir_for_corrector)

            log.info("\n===== Mismatch correction finished.\n")

        if not cfg["common"].developer_mode and os.path.isdir(tmp_configs_dir):
            shutil.rmtree(tmp_configs_dir)

        log.info("")
        if os.path.isdir(os.path.dirname(bh_dataset_filename)):
            log.info(" * Corrected reads are in " + os.path.dirname(bh_dataset_filename) + "/")
        if os.path.isfile(result_contigs_filename):
            log.info(" * Assembled contigs are in " + result_contigs_filename)
        if os.path.isfile(result_scaffolds_filename):
            log.info(" * Assembled scaffolds are in " + result_scaffolds_filename)
        #log.info("")

        #breaking scaffolds
        if os.path.isfile(result_scaffolds_filename):
            if not os.path.isdir(misc_dir):
                os.makedirs(misc_dir)
            result_broken_scaffolds = os.path.join(misc_dir, "broken_scaffolds.fasta")
            threshold = 3
            support.break_scaffolds(result_scaffolds_filename, threshold, result_broken_scaffolds)
            #log.info(" * Scaffolds broken by " + str(threshold) + " Ns are in " + result_broken_scaffolds)

        log.info("")
        log.info("Thank you for using SPAdes!")

        log.info("\n======= SPAdes pipeline finished. Log can be found here: " + log_filename + "\n")
    except Exception, e:
        log.exception(e)
        support.error("exception caught", log)


if __name__ == '__main__':
    main()
