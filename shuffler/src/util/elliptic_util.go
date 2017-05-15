// Copyright 2017 The Fuchsia Authors
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

package util

import (
	"crypto/elliptic"
	"fmt"
	"math/big"
)

// The elliptic curve we are using.
var ellipticCurve elliptic.Curve

// The number of bytes used in the serialized representation of a point on
// the elliptic curve.
var ecSerializationSize int

// The maximum number of bytes needed to store a representation of an element of
// the field over which the elliptic curve group is defined. (Currently this
// will be 32 bytes.)
var ecFieldElementSize int

func init() {
	// If the choice of curve changes we must change the numerical constants
	// above.
	ellipticCurve = elliptic.P256()

	// 32 bytes if we are using P256.
	ecFieldElementSize = (ellipticCurve.Params().BitSize + 7) >> 3

	// We use the "compressed" form in which a point is represented by its
	// x-coordinate (32 bytes, big-endian) with a leading byte indicating
	// which of the two square roots to use for the y-coordinate.
	ecSerializationSize = ecFieldElementSize + 1
}

// Routines for working with Elliptic curves.
//
// The code in this file was largely cribbed from some code written by Bryan Ford
// in https://go-review.googlesource.com/c/1883/.

// Use the curve equation to calculate y² given x
func ySquared(curve *elliptic.CurveParams, x *big.Int) *big.Int {

	// y² = x³ - 3x + b
	x3 := new(big.Int).Mul(x, x)
	x3.Mul(x3, x)

	threeX := new(big.Int).Lsh(x, 1)
	threeX.Add(threeX, x)

	x3.Sub(x3, threeX)
	x3.Add(x3, curve.B)
	x3.Mod(x3, curve.P)
	return x3
}

// MarshalCompressed returns the X9.62 compressed serialization of the point
// (x, y), which must be a point on the given |curve|. Returns nil on failure.
func MarshalCompressed(curve elliptic.Curve, x, y *big.Int) []byte {
	ret := make([]byte, ecSerializationSize)
	ret[0] = 2 // compressed point

	xBytes := x.Bytes()
	if len(xBytes) > ecFieldElementSize {
		return nil
	}
	copy(ret[ecSerializationSize-len(xBytes):], xBytes)
	// The first byte of the encoding indicates which square root of y² y is.
	ret[0] += byte(y.Bit(0))
	return ret
}

// Unmarshal returns the point (x, y) on the given |curve| that is encoded
// by |data|, which must be either the compressed or uncompressed X9.62
// serialization of a point on the curve. Returns nil on failure.
func Unmarshal(curve elliptic.Curve, data []byte) (x, y *big.Int) {
	// If the data is uncompressed just invoke elliptic.Unmarshal.
	x, y = elliptic.Unmarshal(curve, data)
	if x != nil && y != nil {
		return
	}
	x = nil
	y = nil
	byteLen := (curve.Params().BitSize + 7) >> 3
	if len(data) != 1+byteLen {
		return
	}
	if (data[0] &^ 1) != 2 {
		return // unrecognized point encoding
	}

	// Based on Routine 2.2.4 in NIST Mathematical Routines paper
	params := curve.Params()
	tx := new(big.Int).SetBytes(data[1 : 1+byteLen])
	y2 := ySquared(params, tx)

	// Compute the square root of y2.
	ty := new(big.Int).ModSqrt(y2, params.P)
	if ty == nil {
		return // y2 is not a quadratic residue: invalid point
	}

	// data[0] encodes which of the two square roots of y2 we want.
	if ty.Bit(0) != uint(data[0]&1) {
		ty.Sub(params.P, ty)
	}

	x, y = tx, ty // valid point: return it
	return
}

// computeSharedKey returns the ECDH shared key corresponding to the given
// public and private keys.
//
// If the public key is g^alpha and the private key is beta then the shared key is
// g^{alpha*beta}.
//
// Since elliptic curve groups use additive notion this will be a scalar
// multiplication instead of an exponentiation. Thus if the public key is
// alpha*g and the private key is beta then the shared key is beta*alpha*g.
//
// More precisely, let (x, y) be the affine coordinates of beta*alpha*g. So
// x and y are elements of the underlying field and so they are integers in
// the range [0, ecFieldElementSize). The shared key is the big-endian bytes
// of x, padded with leading zeroes so as to have length ecFieldElementSize.
func computeSharedKey(publicX, publicY *big.Int, privateKey []byte) []byte {
	xCoord, _ := ellipticCurve.ScalarMult(publicX, publicY, privateKey)
	sharedKey, err := padFieldElement(xCoord.Bytes())
	if err != nil {
		panic(fmt.Sprintf("Cannot compute sharedKey: %v", err))
	}
	return sharedKey
}

// padFieldElement pads |fieldElement| with leading zeroes to a length of
// ecFieldElementSize. Returns the padded value or an error if len(fieldElement)
// is greater than ecFieldElementSize.
func padFieldElement(fieldElement []byte) ([]byte, error) {
	// Pad with leading zeroes to ecFieldElementSize.
	if len(fieldElement) == ecFieldElementSize {
		return fieldElement, nil
	}
	if len(fieldElement) > ecFieldElementSize {
		return nil, fmt.Errorf("len(fieldElement)=%d", len(fieldElement))
	}
	fieldElementPadded := make([]byte, ecFieldElementSize)
	copy(fieldElementPadded[ecFieldElementSize-len(fieldElement):], fieldElement)
	return fieldElementPadded, nil
}
