// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements output formatting for the cobalt config parser.

package config_parser

import (
	"bytes"
	"config"
	"encoding/base64"
	"fmt"
	"regexp"
	"sort"
	"strings"

	"github.com/golang/protobuf/proto"
)

type OutputFormatter func(c *config.CobaltConfig) (outputBytes []byte, err error)

// Outputs the serialized proto.
func BinaryOutput(c *config.CobaltConfig) (outputBytes []byte, err error) {
	buf := proto.Buffer{}
	buf.SetDeterministic(true)
	if err := buf.Marshal(c); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// Outputs the serialized proto base64 encoded.
func Base64Output(c *config.CobaltConfig) (outputBytes []byte, err error) {
	configBytes, err := BinaryOutput(c)
	if err != nil {
		return outputBytes, err
	}
	encoder := base64.StdEncoding
	outLen := encoder.EncodedLen(len(configBytes))

	outputBytes = make([]byte, outLen, outLen)
	encoder.Encode(outputBytes, configBytes)
	return outputBytes, nil
}

type sourceOutputType int

const (
	CPP sourceOutputType = iota
	Dart
)

type sourceOutputter struct {
	buffer     *bytes.Buffer
	outputType sourceOutputType
	varName    string
	namespaces []string
}

func newSourceOutputter(outputType sourceOutputType, varName string) *sourceOutputter {
	return newSourceOutputterWithNamespaces(outputType, varName, []string{})
}

func newSourceOutputterWithNamespaces(outputType sourceOutputType, varName string, namespaces []string) *sourceOutputter {
	return &sourceOutputter{
		buffer:     new(bytes.Buffer),
		outputType: outputType,
		varName:    varName,
		namespaces: namespaces,
	}
}

func (so *sourceOutputter) writeLine(str string) {
	so.buffer.WriteString(str + "\n")
}

func (so *sourceOutputter) writeLineFmt(format string, args ...interface{}) {
	so.writeLine(fmt.Sprintf(format, args...))
}

func (so *sourceOutputter) writeComment(comment string) {
	for _, comment_line := range strings.Split(comment, "\n") {
		so.writeLineFmt("// %s", strings.TrimLeft(comment_line, " "))
	}
}

func (so *sourceOutputter) writeCommentFmt(format string, args ...interface{}) {
	so.writeComment(fmt.Sprintf(format, args...))
}

func (so *sourceOutputter) writeGenerationWarning() {
	so.writeCommentFmt(`This file was generated by Cobalt's Config parser based on the configuration
YAML in the cobalt_config repository. Edit the YAML there to make changes.`)
}

var (
	wordSeparator     = regexp.MustCompile(`[ _]`)
	validFirstChar    = regexp.MustCompile(`^[a-zA-Z_]`)
	invalidIdentChars = regexp.MustCompile(`[^a-zA-Z0-9_]`)
)

// sanitizeIdent takes an ident and replaces all invalid characters with _.
//
// Examples:
//   sanitizeIdent("0Test") = "_0Test"
//   sanitizeIdent("THIS IS A TEST") = "THIS_IS_A_TEST"
//   sanitizeIdent("IHopeThisWorks(DoesIt?)") = "IHopeThisWorks_DoesIT__"
func sanitizeIdent(name string) string {
	if !validFirstChar.MatchString(name) {
		name = "_" + name
	}

	return invalidIdentChars.ReplaceAllString(name, "_")
}

// toIdent converts an arbitrary string into a valid ident (should be valid in
// all supported languages).
//
// Examples:
//
//   toIdent("this is a string") = "ThisIsAString"
//   toIdent("testing_a_string") = "TestingAString"
//   toIdent("more word(s)") = "MoreWord_s_"
func toIdent(name string) string {
	capitalizedWords := strings.Title(wordSeparator.ReplaceAllString(name, " "))
	return sanitizeIdent(strings.Join(strings.Split(capitalizedWords, " "), ""))
}

// writeIdConstants prints out a list of constants to be used in testing. It
// uses the Name attribute of each Metric, Report, and Encoding to construct the
// constants.
//
// For a metric named "SingleString" the constant would be kSingleStringMetricId
// For a report named "Test" the constant would be kTestReportId
// For an encoding named "Forculus" the canstant would be kForculusEncodingId
func (so *sourceOutputter) writeIdConstants(constType string, entries map[uint32]string) {
	if len(entries) == 0 {
		return
	}
	var keys []uint32
	for k := range entries {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })

	so.writeCommentFmt("%s ID Constants", constType)
	for _, id := range keys {
		name := entries[id]
		so.writeComment(name)
		name = toIdent(name)
		switch so.outputType {
		case CPP:
			so.writeLineFmt("const uint32_t k%s%sId = %d;", name, constType, id)
		case Dart:
			so.writeComment("ignore: constant_identifier_names")
			so.writeLineFmt("const int k%s%sId = %d;", name, constType, id)
		}
	}
	so.writeLine("")
}

// writeEnum prints out an enum with a for list of EventCodes (cobalt v1.0 only)
//
// It prints out the event_code string using toIdent, (event_code => EventCode).
// In c++ and Dart, it also creates a series of constants that effectively
// export the enum values. For a metric called "foo_bar" with a event named
// "baz", it would generate the constant:
// "FooBarEventCode_Baz = FooBarEventCode::Baz"
func (so *sourceOutputter) writeEnum(prefix string, suffix string, entries map[uint32]string) {
	if len(entries) == 0 {
		return
	}

	var keys []uint32
	for k := range entries {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })

	enumName := fmt.Sprintf("%s%s", toIdent(prefix), toIdent(suffix))
	so.writeCommentFmt("Enum for %s (%s)", prefix, suffix)
	switch so.outputType {
	case CPP:
		so.writeLineFmt("enum class %s {", enumName)
	case Dart:
		so.writeLineFmt("class %s {", enumName)
	}
	for _, id := range keys {
		name := entries[id]
		name = toIdent(name)
		switch so.outputType {
		case CPP:
			so.writeLineFmt("  %s = %d,", name, id)
		case Dart:
			so.writeLineFmt("  static const int %s = %d;", name, id)
		}
	}
	switch so.outputType {
	case CPP:
		so.writeLine("};")
	case Dart:
		so.writeLine("}")
	}
	for _, id := range keys {
		name := toIdent(entries[id])
		switch so.outputType {
		case CPP:
			so.writeLineFmt("const %s %s_%s = %s::%s;", enumName, enumName, name, enumName, name)
		case Dart:
			so.writeLineFmt("const int %s_%s = %s::%s;", enumName, name, enumName, name)
		}
	}
	so.writeLine("")
}

func (so *sourceOutputter) Bytes() []byte {
	return so.buffer.Bytes()
}

func (so *sourceOutputter) writeLegacyConstants(c *config.CobaltConfig) {
	metrics := make(map[uint32]string)
	for _, metric := range c.MetricConfigs {
		if metric.Name != "" {
			metrics[metric.Id] = metric.Name
		}
	}
	// write out the 'Metric' constants (e.g. kTestMetricId)
	so.writeIdConstants("Metric", metrics)

	reports := make(map[uint32]string)
	for _, report := range c.ReportConfigs {
		if report.Name != "" {
			reports[report.Id] = report.Name
		}
	}
	// write out the 'Report' constants (e.g. kTestReportId)
	so.writeIdConstants("Report", reports)

	encodings := make(map[uint32]string)
	for _, encoding := range c.EncodingConfigs {
		if encoding.Name != "" {
			encodings[encoding.Id] = encoding.Name
		}
	}
	// write out the 'Encoding' constants (e.g. kTestEncodingId)
	so.writeIdConstants("Encoding", encodings)
}

func (so *sourceOutputter) writeV1Constants(c *config.CobaltConfig) error {
	metrics := make(map[uint32]string)
	if len(c.Customers) > 1 || len(c.Customers[0].Projects) > 1 {
		return fmt.Errorf("Cobalt v1.0 output can only be used with a single project config.")
	}
	for _, metric := range c.Customers[0].Projects[0].Metrics {
		if metric.MetricName != "" {
			metrics[metric.Id] = metric.MetricName
		}
	}
	so.writeIdConstants("Metric", metrics)

	for _, metric := range c.Customers[0].Projects[0].Metrics {
		events := make(map[uint32]string)
		for value, name := range metric.EventCodes {
			events[value] = name
		}
		so.writeEnum(metric.MetricName, "EventCode", events)
	}

	return nil
}

func (so *sourceOutputter) writeFile(c *config.CobaltConfig) error {
	so.writeGenerationWarning()

	if so.outputType == CPP {
		so.writeLine("#pragma once")
		so.writeLine("")
	}

	if so.outputType == CPP {
		for _, name := range so.namespaces {
			so.writeLineFmt("namespace %s {", name)
		}
	}

	if len(c.Customers) > 0 {
		so.writeV1Constants(c)
	} else {
		so.writeLegacyConstants(c)
	}

	b64Bytes, err := Base64Output(c)
	if err != nil {
		return err
	}

	so.writeComment("The base64 encoding of the bytes of a serialized CobaltConfig proto message.")
	switch so.outputType {
	case CPP:
		so.writeLineFmt("const char %s[] = \"%s\";", so.varName, b64Bytes)
	case Dart:
		so.writeLineFmt("const String %s = '%s';", so.varName, b64Bytes)
	}

	if so.outputType == CPP {
		for _, name := range so.namespaces {
			so.writeLineFmt("}  // %s", name)
		}
	}

	return nil
}

func (so *sourceOutputter) getOutputFormatter() OutputFormatter {
	return func(c *config.CobaltConfig) (outputBytes []byte, err error) {
		err = so.writeFile(c)
		return so.Bytes(), err
	}
}

// Returns an output formatter that will output the contents of a C++ header
// file that contains a variable declaration for a string literal that contains
// the base64-encoding of the serialized proto.
//
// varName will be the name of the variable containing the base64-encoded serialized proto.
// namespace is a list of nested namespaces inside of which the variable will be defined.
func CppOutputFactory(varName string, namespace []string) OutputFormatter {
	return newSourceOutputterWithNamespaces(CPP, varName, namespace).getOutputFormatter()
}

// Returns an output formatter that will output the contents of a Dart file
// that contains a variable declaration for a string literal that contains
// the base64-encoding of the serialized proto.
//
// varName will be the name of the variable containing the base64-encoded serialized proto.
// namespace is a list of nested namespaces inside of which the variable will be defined.
func DartOutputFactory(varName string) OutputFormatter {
	return newSourceOutputter(Dart, varName).getOutputFormatter()
}
