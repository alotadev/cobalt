// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Shuffler receives ciphertexts from Encoders (end users), buffers them
// according to a policy, and then batch sends them in a random order to an
// Analyzer. The purpose is to break linkability between end users and
// ciphertexts from the Analyzer's point of view. The Analyzer does not know
// which end user produced which ciphertext.

package dispatcher

import "math/rand"

// Policy defines instructions on how to route messages to Analyzers
type Policy struct{}

// Shuffler interface provides functionality to add a policy to a ciphertext.
// It will eventually be sent out to an Analyzer according to a policy.
// TODO(bittau) use protobus directly once they are defined and committed
type Shuffler interface {
	add(policy Policy, ciphertext []byte)
}

// An Analyzer object knows how to send data to a specific Analyzer
type Analyzer interface {
	send(ciphertexts [][]byte)
}

// BasicShuffler is An implementation of a basic shuffler.  It will wait until
// batchsize ciphertexts are received, and then send them in a random order to
// anaylzer
type BasicShuffler struct {
	analyzer  Analyzer
	batchsize int
	// TODO(bittau): NB ciphertexts will go away (or have a different type)
	// once protobufs are defined.  protobufs will be stored directly.
	ciphertexts [][]byte
}

func (s *BasicShuffler) add(policy Policy, ciphertext []byte) {
	s.ciphertexts = append(s.ciphertexts, ciphertext)

	if len(s.ciphertexts) >= s.batchsize {
		s.shuffleAndSend()
	}
}

func (s *BasicShuffler) shuffleAndSend() {
	num := len(s.ciphertexts)
	shuffled := make([][]byte, num)

	// Get a random ordering for all messages.  We assume that the random
	// number generator is appropriately seeded.
	perm := rand.Perm(num)

	for i, rnd := range perm {
		shuffled[i] = s.ciphertexts[rnd]
	}

	// Send to analyzer and clear buffer
	s.analyzer.send(shuffled)
	s.ciphertexts = [][]byte{}
}
