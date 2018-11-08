// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements outputLanguage for Dart
package source_generator

type Dart struct{}

func (_ Dart) getCommentPrefix() string { return "//" }

func (_ Dart) writeExtraHeader(so *sourceOutputter) {}

func (_ Dart) writeEnumBegin(so *sourceOutputter, name ...string) {
	so.writeLineFmt("class %s {", toPascalCase(name...))
}

func (_ Dart) writeEnumEntry(so *sourceOutputter, value uint32, name ...string) {
	so.writeLineFmt("  static const int %s = %d;", toPascalCase(name...), value)
}

func (_ Dart) writeEnumEnd(so *sourceOutputter) {
	so.writeLineFmt("}")
}

func (_ Dart) writeEnumAlias(so *sourceOutputter, enumName, name []string) {
	enum := toPascalCase(enumName...)
	variant := toPascalCase(name...)
	so.writeLineFmt("const int %s_%s = %s::%s;", enum, variant, enum, variant)
}

func (_ Dart) writeNamespaceBegin(so *sourceOutputter, name ...string) {}
func (_ Dart) writeNamespaceEnd(so *sourceOutputter)                   {}

func (_ Dart) writeConstInt(so *sourceOutputter, value uint32, name ...string) {
	so.writeComment("ignore: constant_identifier_names")
	so.writeLineFmt("const int %s = %d;", toCamelCase(name...), value)
}

func (_ Dart) writeStringConstant(so *sourceOutputter, value string, name ...string) {
	so.writeLineFmt("const String %s = '%s';", toCamelCase(name...), value)
}

// Returns an output formatter that will output the contents of a Dart file
// that contains a variable declaration for a string literal that contains
// the base64-encoding of the serialized proto.
//
// varName will be the name of the variable containing the base64-encoded serialized proto.
// namespace is a list of nested namespaces inside of which the variable will be defined.
func DartOutputFactory(varName string) OutputFormatter {
	return newSourceOutputter(Dart{}, varName).getOutputFormatter()
}
