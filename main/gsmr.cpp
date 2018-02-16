#include "gcta.h"
#include "Logger.h"
#include <sstream>
#include <iterator>

bool determine_gwas_file(string input_file) {
    int nelements = 0;
    bool file_type = false;
    string strbuf;

    ifstream file_list(input_file);
    if(!file_list) 
        LOGGER.e(0, "Cannot open file [" + input_file + "] to read.");

    std::getline(file_list, strbuf);
    std::istringstream linebuf(strbuf);
    vector<string> line_elements((istream_iterator<string>(linebuf)), istream_iterator<string>());
    nelements = line_elements.size();

    if(nelements==1) {
        file_type = true;
    } else if(nelements==8) {
        file_type = false;
    } else {
        LOGGER.e(0, "The GWAS summary data should be GCTA-COJO format. Please check.");
    }

    return(file_type);
}

void read_gsmr_file_list(string gsmr_file_list, vector<string> &pheno_name, vector<string> &pheno_file, vector<double> &popu_prev, vector<double> &smpl_prev) {

    ifstream meta_list(gsmr_file_list.c_str());
    if (!meta_list)
        LOGGER.e(0, "Cannot open the file [" + gsmr_file_list + "] to read.");
    
    string strbuf="", prevbuf1="", prevbuf2="";
    // Retrieve the GWAS summary data file
    // The 1st row: the target trait
    int line_number = 1;
    while(std::getline(meta_list, strbuf)) 
    {
        std::istringstream linebuf(strbuf);
        vector<string> line_elements((istream_iterator<string>(linebuf)), istream_iterator<string>());
        
        if(line_elements.size() != 2 && line_elements.size() != 4)
            LOGGER.e(0, "Format of file [" + gsmr_file_list + "] is not correct, line " + to_string(line_number) + ".");
        
        pheno_name.push_back(line_elements[0]); pheno_file.push_back(line_elements[1]);
        
        // prevelance
        double d_prev1 = nan(""), d_prev2 = nan("");
        if(line_elements.size()==4) {
            prevbuf1 = line_elements[2]; prevbuf2 = line_elements[3];
            StrFunc::to_upper(prevbuf1); StrFunc::to_upper(prevbuf2);
            // available, 1 - sample prevelance, 2 - population prevelance
            if(prevbuf1 != "NA"  &&  prevbuf1!= "NAN" && prevbuf1!= ".") {
                d_prev1 = atof(prevbuf1.c_str());
                if(d_prev1 < 0 || d_prev1 > 1)
                    LOGGER.e(0, "Invalid sample prevalence for [" + pheno_name[line_number] + "].");
            }
            if(prevbuf2 != "NA"  &&  prevbuf2!= "NAN" && prevbuf2 != ".") {
                d_prev2 = atof(prevbuf2.c_str());
                if(d_prev2 < 0 || d_prev2 > 1)
                    LOGGER.e(0, "Invalid population prevalence for [" + pheno_name[line_number] + "].");
            }
        }
        smpl_prev.push_back(d_prev1); popu_prev.push_back(d_prev2);
        line_number++;
    }
    meta_list.close();
}

void gcta::read_gsmrfile(string expo_file_list, string outcome_file_list, double clump_thresh1, double gwas_thresh) {
    int i = 0, j = 0;
    vector<string> expo_gwas_file, outcome_gwas_file, gwas_data_file, pheno_name_buf;
    vector<double> popu_prev_buf, smpl_prev_buf;

    // Read the SNPs
    // Exposure
    LOGGER.i(0, "\nReading GWAS SNPs for exposure(s) from [" + expo_file_list + "].");
    popu_prev_buf.clear(); smpl_prev_buf.clear(); pheno_name_buf.clear();
    read_gsmr_file_list(expo_file_list, pheno_name_buf, expo_gwas_file, popu_prev_buf, smpl_prev_buf);
    _meta_popu_prev = popu_prev_buf; _meta_smpl_prev = smpl_prev_buf; 
    _gwas_trait_name = pheno_name_buf;
    gwas_data_file = expo_gwas_file; 

    _expo_num = pheno_name_buf.size();

    vector<string> snplist;
    for(i=0; i<_expo_num; i++) {
        snplist=read_snp_metafile(expo_gwas_file[i], clump_thresh1);
        if(i==0) init_meta_snp_map(snplist);
        else update_meta_snp_map(snplist, _meta_snp_name_map, _meta_snp_name, _meta_remain_snp);
    }

    // Outcome
    LOGGER.i(0, "Reading GWAS SNPs for outcome(s) from [" + outcome_file_list + "].");
    popu_prev_buf.clear(); smpl_prev_buf.clear(); pheno_name_buf.clear();
    read_gsmr_file_list(outcome_file_list, pheno_name_buf, outcome_gwas_file, popu_prev_buf, smpl_prev_buf);
    _meta_popu_prev.insert(_meta_popu_prev.end(), popu_prev_buf.begin(), popu_prev_buf.end());
    _meta_smpl_prev.insert(_meta_smpl_prev.end(), smpl_prev_buf.begin(), smpl_prev_buf.end());
    _gwas_trait_name.insert(_gwas_trait_name.end(), pheno_name_buf.begin(), pheno_name_buf.end());
    gwas_data_file.insert(gwas_data_file.end(), outcome_gwas_file.begin(), outcome_gwas_file.end());

    _outcome_num = pheno_name_buf.size();

    for(i=0; i<_outcome_num; i++) {
        snplist=read_snp_metafile(outcome_gwas_file[i], -9);
        update_id_map_kp(snplist, _meta_snp_name_map, _meta_remain_snp);
    }

    // Initialization of variables
    int nsnp = _meta_snp_name_map.size(), npheno = _expo_num + _outcome_num;
    vector<vector<string>> snp_a1, snp_a2;
    eigenMatrix snp_freq; 

    init_gwas_variable(snp_a1, snp_a2, snp_freq, _meta_snp_b, _meta_snp_se, _meta_snp_pval, _meta_snp_n_o, npheno, nsnp); 

    // reset SNP variables
    update_meta_snp(_meta_snp_name_map, _meta_snp_name, _meta_remain_snp);
    
    LOGGER.i(0, to_string(nsnp) + " SNPs are significantly associated with at least one exposure.");

    // Reading the summary data
    _meta_vp_trait.resize(npheno);
    _snp_val_flag.clear(); _snp_val_flag.resize(npheno);
    for(i=0; i<npheno; i++) {
        _snp_val_flag[i].resize(nsnp);
        for(j=0; j<nsnp; j++) _snp_val_flag[i][j] = false;
    }

    LOGGER.i(0, "Reading GWAS summary-level statistics for exposure(s) and outcome(s).");
    // Summary data
    for(i=0; i<npheno; i++) {
        eigenVector snp_freq_buf(nsnp), snp_b_buf(nsnp), snp_se_buf(nsnp), snp_pval_buf(nsnp), snp_n_buf(nsnp);
        _meta_vp_trait[i] = read_single_metafile(gwas_data_file[i], _meta_snp_name_map,  snp_a1[i], snp_a2[i], snp_freq_buf, snp_b_buf, snp_se_buf, snp_pval_buf, snp_n_buf, _snp_val_flag[i]);
        if(_meta_vp_trait[i] < 0) LOGGER.e(0, "Negative phenotypic variance of trait " + _gwas_trait_name[i] + ".");
        snp_freq.col(i) = snp_freq_buf;
        _meta_snp_b.col(i) = snp_b_buf;
        _meta_snp_se.col(i) = snp_se_buf;
        _meta_snp_pval.col(i) = snp_pval_buf;
        _meta_snp_n_o.col(i) = snp_n_buf;
    }
  
    // QC of SNPs
    LOGGER.i(0, "Filtering out SNPs with multiple alleles or missing value ...");
    vector<string>::iterator iter1 = _gwas_trait_name.begin(), iter2 = _gwas_trait_name.end();
    vector<string> badsnps, expo_pheno_name(iter1, iter1+_expo_num-1), outcome_pheno_name(iter1+_expo_num, iter2);
    badsnps = remove_bad_snps(_meta_snp_name, _meta_remain_snp, _snp_val_flag, snp_a1, snp_a2, snp_freq,  _meta_snp_b, _meta_snp_se, _meta_snp_pval, _meta_snp_n_o, 
                              _snp_name_map, _allele1, _allele2, outcome_pheno_name, _outcome_num, expo_pheno_name, _expo_num, _out);

    if(badsnps.size()>0) {
        update_id_map_rm(badsnps, _snp_name_map, _include);
        update_mtcojo_snp_rm(badsnps, _meta_snp_name_map, _meta_remain_snp);
    }

    // For output
    _meta_snp_a1 = snp_a1[0]; _meta_snp_a2 = snp_a2[0];
    _meta_snp_freq = snp_freq.col(0);
  
    nsnp = _meta_remain_snp.size();
    if(nsnp<1) LOGGER.e(0, "None SNPs are retained for the GSMR analysis.");
    else LOGGER.i(0, to_string(nsnp) + " SNPs are retained after filtering.");

    // Only keep SNPs with p-value < threshold
    double pval_thresh =  gwas_thresh < clump_thresh1 ?  gwas_thresh : clump_thresh1;
    vector<string> keptsnps;
    keptsnps = filter_meta_snp_pval(_meta_snp_name, _meta_remain_snp, _meta_snp_pval, 0, npheno, pval_thresh);
    if(keptsnps.size()>0) {
        update_id_map_kp(keptsnps, _snp_name_map, _include);
    }
    LOGGER.i(0, to_string(_include.size()) + " significant SNPs are in common with those in the reference sample.\n");
}

void gsmr_rst_output(string output_file, vector<vector<double>> bxy_est, int gsmr_alg_flag, vector<string> pheno_name, int expo_num_buf, int outcome_num_buf) {
    bool output_forward = false, output_reverse = false;
    int i=0, j=0, k=0;

    switch(gsmr_alg_flag) {
        case 0 : output_forward=true; break;
        case 1 : output_reverse=true; break;
        case 2 : output_forward=true; output_reverse=true; break;
    }

    ofstream ofile(output_file.c_str());
    if (!ofile) LOGGER.e(0, "Cannot open the file [" + output_file + "] to write.");

    ofile << "Exposure\tOutcome\tbxy\tse\tp\tnsnp" <<endl;
    k=0; 
    if(output_forward) {
        for(i=0; i<expo_num_buf; i++) {
            for(j=0; j<outcome_num_buf; j++, k++) {
                ofile << pheno_name[i] << "\t" << pheno_name[expo_num_buf+j] << "\t"
                      << bxy_est[0][k] << "\t" << bxy_est[1][k] << "\t" 
                      << bxy_est[2][k] << "\t" << bxy_est[3][k] << endl;
            }
        }
    }
    if(output_reverse) {
        for(i=0; i<outcome_num_buf; i++) {
            for(j=0; j<expo_num_buf; j++, k++) {
                ofile << pheno_name[expo_num_buf+i] << "\t" << pheno_name[j] << "\t"
                      << bxy_est[0][k] << "\t" << bxy_est[1][k] << "\t" 
                      << bxy_est[2][k] << "\t" << bxy_est[3][k] << endl;
            }
        }
    }
    ofile.close();
}

void gcta::gsmr(int gsmr_alg_flag, double clump_thresh1, double clump_thresh2, double clump_wind_size, double clump_r2_thresh, double gwas_thresh, double heidi_thresh, double ld_fdr_thresh, int nsnp_heidi, int nsnp_gsmr, bool heidi_flag) {
    vector<vector<double>> bxy_est;

    // Calculate allele frequency
   if (_mu.empty()) calcu_mu();
   
    switch(gsmr_alg_flag) {
        case 0 : { 
            bxy_est = forward_gsmr(clump_thresh1, clump_thresh2, clump_wind_size, clump_r2_thresh, gwas_thresh, heidi_thresh, ld_fdr_thresh, nsnp_heidi, nsnp_gsmr, heidi_flag); 
            break;
        }
        case 1 : {
            bxy_est = reverse_gsmr(clump_thresh1, clump_thresh2, clump_wind_size, clump_r2_thresh, gwas_thresh, heidi_thresh, ld_fdr_thresh, nsnp_heidi, nsnp_gsmr, heidi_flag); 
            break;
        }
        case 2 : {
            vector<vector<double>> bxy_est_buf;
            bxy_est = forward_gsmr(clump_thresh1, clump_thresh2, clump_wind_size, clump_r2_thresh, gwas_thresh, heidi_thresh, ld_fdr_thresh, nsnp_heidi, nsnp_gsmr, heidi_flag); 
            bxy_est_buf = reverse_gsmr(clump_thresh1, clump_thresh2, clump_wind_size, clump_r2_thresh, gwas_thresh, heidi_thresh, ld_fdr_thresh, nsnp_heidi, nsnp_gsmr, heidi_flag);
            int i = 0;
            for(i=0; i<4; i++) bxy_est[i].insert(bxy_est[i].end(), bxy_est_buf[i].begin(), bxy_est_buf[i].end());
            break;
        }
    }

    string output_filename = _out + ".gsmr";
    LOGGER.i(0, "\nSaving the GSMR analysis results of " + to_string(_expo_num) + " exposure(s) and "
                 + to_string(_outcome_num) + " outcome(s) to [" + output_filename + "] ...");
    gsmr_rst_output(output_filename, bxy_est, gsmr_alg_flag, _gwas_trait_name, _expo_num, _outcome_num);

    LOGGER.i(0, "\nGSMR analysis completed.");
}

vector<vector<double>> gcta::forward_gsmr(double clump_thresh1, double clump_thresh2, double clump_wind_size, double clump_r2_thresh, double gwas_thresh, double heidi_thresh, double ld_fdr_thresh, int nsnp_heidi, int nsnp_gsmr, bool heidi_flag) {
    int i=0, j=0, k=0, t=0, m=_expo_num*_outcome_num, nsnp = _meta_remain_snp.size();
    vector<bool> snp_pair_flag(nsnp);
    vector<vector<double>> bxy_est;
     
    bxy_est.resize(4);
    for(i=0; i<4; i++) bxy_est[i].resize(m);
    
    // GSMR analysis
    vector<double> gsmr_rst(3);
    for(i=0, t=0; i<_expo_num; i++) {
        for(j=0; j<_outcome_num; j++, t++) {
            for(k=0; k<nsnp; k++) snp_pair_flag[k] = (int)((_snp_val_flag[i][k] + _snp_val_flag[j+_expo_num][k])/2);
            LOGGER.i(0, "\nForward GSMR analysis for exposure #" + to_string(i+1) + " and outcome #" + to_string(j+1) + " ...");
            gsmr_rst =  gsmr_meta(_meta_snp_b.col(i), _meta_snp_se.col(i), _meta_snp_pval.col(i), 
                                  _meta_snp_b.col(j+_expo_num), _meta_snp_se.col(j+_expo_num),  snp_pair_flag, clump_thresh1, clump_thresh2, clump_wind_size, clump_r2_thresh, gwas_thresh, heidi_thresh, ld_fdr_thresh, nsnp_heidi, nsnp_gsmr, heidi_flag);
            if(std::isnan(gsmr_rst[3]))
                LOGGER.w(0, "Not enough SNPs to perform the GSMR analysis. At least " + to_string(nsnp_gsmr) + " SNPs are required. Skipping...");
            else
                LOGGER.i(0, "Forward GSMR analysis for exposure #" + to_string(j+1) + " and outcome #" + to_string(i+1) + " completed.");
            for(k=0; k<4; k++) bxy_est[k][t] = gsmr_rst[k];
        }
    }
    return(bxy_est);
}

vector<vector<double>> gcta::reverse_gsmr(double clump_thresh1, double clump_thresh2, double clump_wind_size, double clump_r2_thresh, double gwas_thresh, double heidi_thresh, double ld_fdr_thresh, int nsnp_heidi, int nsnp_gsmr, bool heidi_flag) {
     int i=0, j=0, k=0, t=0, m=_expo_num*_outcome_num, nsnp = _meta_remain_snp.size();
     vector<bool> snp_pair_flag(nsnp);
     vector<vector<double>> bxy_est;
     
     bxy_est.resize(4);
     for(i=0; i<4; i++) bxy_est[i].resize(m);
    
    // GSMR analysis
    vector<double> gsmr_rst(4);
    for(i=0, t=0; i<_outcome_num; i++) {
        for(j=0; j<_expo_num; j++, t++) {
            for(k=0; k<nsnp; k++) snp_pair_flag[k] = (int)((_snp_val_flag[i+_expo_num][k] + _snp_val_flag[j][k])/2);
            LOGGER.i(0, "\nReverse GSMR analysis for exposure #" + to_string(j+1) + " and outcome #" + to_string(i+1) + " ...");
            gsmr_rst =  gsmr_meta(_meta_snp_b.col(i+_expo_num), _meta_snp_se.col(i+_expo_num), _meta_snp_pval.col(i+_expo_num), 
                                  _meta_snp_b.col(j), _meta_snp_se.col(j), snp_pair_flag, clump_thresh1, clump_thresh2, clump_wind_size, clump_r2_thresh, gwas_thresh, heidi_thresh, ld_fdr_thresh, nsnp_heidi, nsnp_gsmr, heidi_flag);              
            if(std::isnan(gsmr_rst[3])) 
                LOGGER.w(0, "Not enough SNPs to perform the GSMR analysis. At least " + to_string(nsnp_gsmr) + " SNPs are required. Skipping...");
            else
                LOGGER.i(0, "Reverse GSMR analysis for exposure #" + to_string(j+1) + " and outcome #" + to_string(i+1) + " completed.");
            for(k=0; k<4; k++) bxy_est[k][t] = gsmr_rst[k];            
        }
    }
    return(bxy_est);
}