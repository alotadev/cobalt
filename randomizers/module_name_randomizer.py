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

import randomizer
import utils.file_util as file_util


class ModuleNameRandomizer:
  """ A Randomizer that extracts module name from |Entry| and applies
  randomized response algorithms to it to emit noised reports. Additionally,
  it uses user_id from |Entry| to setup the secret in the RAPPOR encoder.
  (The secret is irrelevant for the purposes of this demo.)
  """

  def randomize(self, entries):
    """ A Randomizer that extracts module name from |Entry| and applies
    randomized response algorithms to it to emit noised reports.

    This function does not return anything but it writes a file
    into the "r_to_s" output directory containing the randomizer output.

    Args:
      entries {list of Entry}: The entries to be randomized.
    """
    randomizer.randomizeUsingRappor(entries,
        [(1, # module name index in |Entry|
         True, #use bloom filters
         file_util.RAPPOR_MODULE_NAME_CONFIG # rappor config file name
        )],
        file_util.MODULE_NAME_RANDOMIZER_OUTPUT_FILE_NAME,
        );
