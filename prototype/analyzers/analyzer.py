#!/usr/bin/env python
# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Runs all of the analyzers. This file also contains utilities common to
all analyzers.
"""

import csv
import logging
import os
import shutil
import subprocess
import sys

THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.abspath(os.path.join(THIS_DIR, os.path.pardir))
sys.path.insert(0, ROOT_DIR)
THIRD_PARTY_DIR = os.path.join(ROOT_DIR, "third_party")
ALGORITHMS_DIR = os.path.join(ROOT_DIR, "algorithms")

import algorithms.forculus.forculus as forculus
import algorithms.laplace.laplacian as laplacian
import algorithms.rappor.sum_bits as sum_bits
import city_analyzer
import hour_analyzer
import module_name_analyzer
import help_query_analyzer
import url_analyzer
import utils.data as data
import utils.file_util as file_util
import utils.public_key_crypto_helper as crypto_helper
import third_party.rappor.client.python.rappor as rappor
import third_party.rappor.bin.hash_candidates as hash_candidates

_logger = logging.getLogger()

# Should public key encryption be used for communication between the
# Randomizers and the Analyzers via the Shufflers?
_use_public_key_encryption=False

def analyzeUsingForculus(input_file, config_file, output_file):
  ''' A helper function that may be invoked by individual analyzers. It reads
  input data in the form of a CSV file (that is generated as output from the
  preceeding shuffle operation in the pipeline), analyzes the data using
  Forculus, and then writes output to another CSV file to be consumed by the
  visualizer at the end of the pipeline.

  It uses Forculus to decrypt those entries that occur more than |threshold|
  times, where |threshold| is read from the config file.

  Args:
    input_file {string}: The simple name of the CSV file to be read from
    the 's_to_a' directory.

    config_file {string}: The simple name of the Forculus config file used
    for analyzing the data from the input file.

    output_file {string}: The simple name of the CSV file to be written in
    the 'out' directory, that is consumed by the visualizer.
  '''
  with file_util.openFileForReading(config_file, file_util.CONFIG_DIR) as cf:
    config = forculus.Config.from_csv(cf)

  decrypt_on_analyzer=None
  if _use_public_key_encryption:
    ch = crypto_helper.CryptoHelper()
    decrypt_on_analyzer=ch.decryptOnAnalyzer

  with file_util.openForAnalyzerReading(input_file) as input_f:
    with file_util.openForWriting(output_file) as output_f:
      forculus_evaluator = forculus.ForculusEvaluator(config.threshold, input_f)
      forculus_evaluator.ComputeAndWriteResults(output_f,
          additional_decryption_func=decrypt_on_analyzer)

def analyzeUsingRAPPOR(input_file, config_file, output_file,
                       metric_name="metric",
                       candidates_file_name=None,
                       basic_map_file_name=None,
                       assoc_options=None,
                       use_basic_rappor=False,
                       for_private_release=False):
  ''' Run the RAPPOR analysis using the given inputs.

  Args:
    input_file: {string} The simple name of the CSV file to be read from the
      's_to_a' directory.

    config_file: {string} The simple name of the RAPPOR config file to be read
        from the 'config' directory

    output_file: {string} The simple name of the CSV file to be written in
        the 'out' directory

    metric_name: {string} A short name for the metric being computed. It will
      be used to form the name of temporary files and so it should contain
      only letters and underscores.

    candidates_file_name: {string} The simple name of the RAPPOR candidates
      file to be read from the 'config' directory. This must be none-None if and
      only if |use_basic_rappor|=False.

    basic_map_file_name: {string} The name of the pre-computed map file
      to be read from the 'config' directory. This must be none-None if and
      only if |use_basic_rappor|=True.

    use_basic_rappor {bool}: If True use basic RAPPOR analysis
      (no Bloom filters.)

    for_private_release {bool}: If True then in addition to doing the RAPPOR
      decoding, Laplace noise is also added to the analyzed output. We use
      different input, output and config files in this case.

    assoc_options {dictionary}: If this parameter is not None it indicates that
      a special two-dimensional RAPPOR analysis should be performed. This
      analysis uses correlation to compute average scores for the second
      dimension as a function of the first dimension. This assumes that the
      second dimension uses basic RAPPOR with values being integers in the
      range 1 to N for some positive integer N.

      In that case this parameter should be a dictionary of options for the
      correlation analysis. Also in this case there are additional requirements
      on the other parameters of this method:
        - The |input_file| must contain the data for *both* variables. The
        - |config_file| should contain the RAPPOR params for the *first*
          variable.
        - The |output_file| should be named appropriately for *both* variables.
        - The |metric_name| should be named appropriately for *both* variables.
        - The |candidates_file_name| should refer to the candidates of
          the *first* variable.

      The dictionary should contain the following string keys and values:
        - "metric_name": The final correlation analysis metric name such as
          |CityAverageRating|. Note that this is a different value than the
          parameter to this function called |metric_name|. This "metric_name"
          refers to a field in the csv file specified by "config_file" below.

        - "vars": A list of two variable names used for correlation during
          analysis such as ['city_name', 'rating']. These strings refer to
          fields in the csv file specified by "config_file" below.

        - "config_file": The simple name of the CSV file specifying the
          correlation configuration parameters.
  '''
  # First get the RAPPOR config params.
  with file_util.openFileForReading(
    config_file, file_util.CONFIG_DIR) as cf:
    config_params = rappor.Params.from_csv(cf)

  map_file_prefix = metric_name
  if assoc_options is not None:
    # If we are doing a correlation analysis then the map file corresponds
    # only to the first variable.
    assert len(assoc_options["vars"]) == 2
    map_file_prefix = assoc_options["vars"][0]
  map_file_name = '%s_map_file.csv' % map_file_prefix
  map_file = os.path.join(file_util.CACHE_DIR, map_file_name)

  if use_basic_rappor:
    # Because we are using basic RAPPOR the map file is a static file in the
    # config directory--we do not have to generate a map file from a candidate
    # file. But we copy it into the cache directory because the R script will
    # write a .rda file in the same directory and we don't want it to write
    # into the config directory.
    assert basic_map_file_name is not None
    assert candidates_file_name is None
    basic_map_file = os.path.join(file_util.CONFIG_DIR, basic_map_file_name)
    _copyFileIfNecessary(basic_map_file, map_file)
  else:
    assert basic_map_file_name is None
    assert candidates_file_name is not None
    _generateMapFileIfNecessary(map_file, map_file_name, candidates_file_name,
        config_params)

  # Decrypt the input file contents from shuffler if necessary.
  decrypt_on_analyzer=None
  if _use_public_key_encryption:
    ch = crypto_helper.CryptoHelper()
    decrypt_on_analyzer = ch.decryptOnAnalyzer

  # Build the command string to invoke the appropriate R script.
  if assoc_options is None:
    # Use generic RAPPOR analysis
    # Compute counts per cohort and write into analyzer temp directory.
    counts_file_name = '%s_count_file.csv' % metric_name
    with file_util.openForAnalyzerReading(input_file) as input_f:
      with file_util.openForAnalyzerTempWriting(counts_file_name) as output_f:
        sum_bits.sumBits(config_params, input_f, output_f, decrypt_on_analyzer)
    cmd = _buildRAPPORAnalysisCmd(counts_file_name, config_file, map_file)
  else:
    # Use correlation based RAPPOR analysis
    if decrypt_on_analyzer is not None:
      # Decrypt the input contents into a temporary file before invoking the
      # RAPPOR script.
      decrypted_input_file = ('%s_decrypted_shuffler_out.csv' % metric_name)
      with file_util.openForAnalyzerReading(input_file) as input_f:
        with file_util.openForAnalyzerTempWriting(
            decrypted_input_file) as tmp_output_f:
          reader = csv.reader(input_f)
          writer = csv.writer(tmp_output_f)
          for i, row in enumerate(reader):
            # The tuple read from csv_in represents a cipher text. Pass the
            # elements of that tuple as arguments to the decryption function
            # receiving back the plaintext which is a single string that is a
            # comma-separated list of fields. Split that string into fields and
            # use that as the value of the read row.
            writer.writerow(decrypt_on_analyzer(*row).split(","))
      reports_file = os.path.abspath(os.path.join(
          file_util.ANALYZER_TMP_OUT_DIR, decrypted_input_file))
    else:
      reports_file = os.path.abspath(os.path.join(file_util.S_TO_A_DIR,
                                                  input_file))

    cmd = _buildRAPPORCorrelationAnalysisCmd(
        assoc_options['config_file'],
        map_file,
        reports_file,
        assoc_options['metric_name'],
        assoc_options['vars'])

  # Then we change into the Rappor directory and execute the command.
  savedir = os.getcwd()
  os.chdir(ALGORITHMS_DIR)
  os.environ["RAPPOR_REPO"] = os.path.join(THIRD_PARTY_DIR, 'rappor/')

  # We supress the output from the R script unless it fails.
  try:
    subprocess.check_output(cmd, stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
    print "\n**** Error running RAPPOR decode script:"
    print e.output
    raise Exception('Fatal error. Cobalt pipeline terminating.')
  os.chdir(savedir)

  if assoc_options or use_basic_rappor or for_private_release == False:
    # Simply copy the results file to the out directory and return.
    results_file_name = 'assoc-results.csv' if assoc_options else 'results.csv'
    src = os.path.abspath(os.path.join(
        file_util.ANALYZER_TMP_OUT_DIR, results_file_name))
    dst = os.path.abspath(os.path.join(
        file_util.OUT_DIR, output_file))
    shutil.copyfile(src, dst)
    return

  # Read in the results of the RAPPOR analysis
  with file_util.openFileForReading('results.csv',
      file_util.ANALYZER_TMP_OUT_DIR) as f:
    reader = csv.reader(f)
    results = [result for result in reader]

  # If requested, add Laplacian noise in order to affect differentially
  # private release.
  if for_private_release:
    # Here we are using  a Laplacian with lambda = 1. This gives use
    # differential privacy with epsilon = 1.
    laplacian_distribution = laplacian.Laplacian(1)
    # We skip row 1 of results because it is a header row.
    for result in results[1:]:
      # The estimated count is in column 1 of the RAPPOR results. Since
      # the values are counts we round to a whole number.
      result[1] = int(round(int(result[1]) + laplacian_distribution.sample()))

  # Write the final output file.
  with file_util.openForWriting(output_file) as f:
    writer = csv.writer(f)
    for result in results:
      writer.writerow(result)

def _buildRAPPORCorrelationAnalysisCmd(assoc_config_file, map1_file,
    reports_file, metric_name, vars):
  ''' Invoke the R script 'decode_assoc_averages.R' for running correlation
      analysis using multiple params.
  '''
  # Create the temp directory.
  file_util.ensureDir(file_util.ANALYZER_TMP_OUT_DIR)

  # First we build the command string.
  rappor_avg_decode_script = os.path.join(ALGORITHMS_DIR,
      'rappor', 'decode_assoc_averages.R')
  schema_file = os.path.abspath(os.path.join(
                file_util.CONFIG_DIR,
                assoc_config_file))

  em_executable_file = os.path.abspath(os.path.join(THIRD_PARTY_DIR,
                                       'rappor', 'analysis',
                                       'cpp', '_tmp', 'fast_em'))
  if os.path.isfile(em_executable_file) == False:
    exception_str = 'Expect fast_em executable at %s.' % em_executable_file
    exception_str += (' Terminating Correlations Analyzer.'
                      ' Please run ./cobalt.py build to build'
                      ' fast_em executable.\n')

    raise Exception('\n****' + exception_str)

  cmd = [rappor_avg_decode_script,
         '--metric-name', metric_name,
         '--schema', schema_file,
         '--reports', reports_file,
         '--params-dir', file_util.CONFIG_DIR,
         '--var1', vars[0],
         '--var2', vars[1],
         '--map1', map1_file,
         '--create-cat-map',
         '--max-em-iters', "1000",
         '--num-cores', "2",
         '--output-dir', file_util.ANALYZER_TMP_OUT_DIR,
         '--em-executable', em_executable_file
         ]
  return cmd

def _buildRAPPORAnalysisCmd(counts_file_name, config_file, map_file):
  rappor_decode_script = os.path.join(THIRD_PARTY_DIR,
      'rappor', 'bin', 'decode_dist.R')
  counts_file = os.path.abspath(os.path.join(file_util.ANALYZER_TMP_OUT_DIR,
      counts_file_name))
  params_file =  os.path.abspath(os.path.join(file_util.CONFIG_DIR,
      config_file))
  cmd = [rappor_decode_script, '--map', map_file, '--counts', counts_file,
         '--params', params_file, '--output-dir',
         file_util.ANALYZER_TMP_OUT_DIR]
  return cmd

def _generateMapFileIfNecessary(map_file, map_file_name, candidates_file_name,
                                params):
  ''' Generates the map file in the cache directory if there is not already
  an up-to-date version.
  '''
  candidate_file = os.path.join(file_util.CONFIG_DIR, candidates_file_name)

  # Generate the map file unless it already exists and has a modified
  # timestamp later than that of the candidate file. We allow a buffer of
  # 10 seconds in case the timestamps are out of sync for some reason.
  if (not os.path.exists(map_file) or
      os.path.getmtime(map_file) < os.path.getmtime(candidate_file) + 10):
    with file_util.openFileForReading(candidates_file_name,
        file_util.CONFIG_DIR) as cand_f:
      with file_util.openFileForWriting(map_file_name,
          file_util.CACHE_DIR) as map_f:
        _logger.debug('Generating a RAPPOR map file at %s based on '
                      'candidate file at %s' % (map_file, candidate_file))
        hash_candidates.HashCandidates(params, cand_f, map_f)

def _copyFileIfNecessary(from_file, to_file):
  ''' Copies from_file to to_file if from_file has been modified more
  recently than to_file.
  '''
  if (not os.path.exists(to_file) or
      os.path.getmtime(to_file) < os.path.getmtime(from_file) + 10):
    shutil.copyfile(from_file, to_file)

def runAllAnalyzers(use_public_key_encryption=False):
  """Runs all of the analyzers.

  This function does not return anything but it invokes all of the
  analyzers each of which will read a file from the 's_to_a' directory
  and write a file in the top level out directory.

  Args:
    use_public_key_encryption {boolean}: Should public key encrytpion be
    used to encrypt communication between the Randomizers and the Analyzers
    via the shufflers?
  """

  global _use_public_key_encryption
  _use_public_key_encryption = use_public_key_encryption

  print "Running the help-query analyzer..."
  hq_analyzer = help_query_analyzer.HelpQueryAnalyzer()
  hq_analyzer.analyze()

  print "Running the city/ratings analyzer..."
  cr_analyzer = city_analyzer.CityRatingsAnalyzer()
  cr_analyzer.analyze()

  print "Running the module names analyzer..."
  mn_analyzer = module_name_analyzer.ModuleNameAnalyzer()
  mn_analyzer.analyze()

  print("Running the module names analyzer with differentially private "
        "release...")
  mn_analyzer.analyze(for_private_release=True)

  print "Running the hour-of-day analyzer..."
  hd_analyzer = hour_analyzer.HourOfDayAnalyzer()
  hd_analyzer.analyze()

  print "Running the url analyzer..."
  u_analyzer = url_analyzer.UrlAnalyzer()
  u_analyzer.analyze()

def main():
  runAllAnalyzers()

if __name__ == '__main__':
  main()
