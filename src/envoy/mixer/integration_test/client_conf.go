// Copyright 2017 Istio Authors. All Rights Reserved.
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
//

package test

import (
	mpb "istio.io/api/mixer/v1"
	mccpb "istio.io/api/mixer/v1/config/client"
)

var (
	MeshIp1 = []byte{1, 1, 1, 1}
	MeshIp2 = []byte{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 204, 152, 189, 116}
	MeshIp3 = []byte{0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8}
)

type V2Conf struct {
	HttpServerConf *mccpb.HttpClientConfig
	HttpClientConf *mccpb.HttpClientConfig
	TcpServerConf  *mccpb.TcpClientConfig
}

func GetDefaultV2Conf() *V2Conf {
	return &V2Conf{
		HttpServerConf: GetDefaultHttpServerConf(),
		HttpClientConf: GetDefaultHttpClientConf(),
		TcpServerConf:  GetDefaultTcpServerConf(),
	}
}

func GetDefaultHttpServerConf() *mccpb.HttpClientConfig {
	v2 := &mccpb.HttpClientConfig{
		MixerAttributes: &mpb.Attributes{
			Attributes: map[string]*mpb.Attributes_AttributeValue{
				"mesh1.ip":         {Value: &mpb.Attributes_AttributeValue_BytesValue{MeshIp1}},
				"target.uid":       {Value: &mpb.Attributes_AttributeValue_StringValue{"POD222"}},
				"target.namespace": {Value: &mpb.Attributes_AttributeValue_StringValue{"XYZ222"}},
			},
		},
		ServiceConfigs: map[string]*mccpb.ServiceConfig{},
	}
	service := ":default"
	v2.DefaultDestinationService = service
	v2.ServiceConfigs[service] = &mccpb.ServiceConfig{
		MixerAttributes: &mpb.Attributes{
			Attributes: map[string]*mpb.Attributes_AttributeValue{
				"mesh2.ip":    {Value: &mpb.Attributes_AttributeValue_BytesValue{MeshIp2}},
				"target.user": {Value: &mpb.Attributes_AttributeValue_StringValue{"target-user"}},
				"target.name": {Value: &mpb.Attributes_AttributeValue_StringValue{"target-name"}},
			},
		},
		// TODO per-service HttpApiApsec, QuotaSpec
	}
	return v2
}

func GetDefaultHttpClientConf() *mccpb.HttpClientConfig {
	v2 := &mccpb.HttpClientConfig{
		ForwardAttributes: &mpb.Attributes{
			Attributes: map[string]*mpb.Attributes_AttributeValue{
				"mesh3.ip":         {Value: &mpb.Attributes_AttributeValue_BytesValue{MeshIp3}},
				"source.uid":       {Value: &mpb.Attributes_AttributeValue_StringValue{"POD11"}},
				"source.namespace": {Value: &mpb.Attributes_AttributeValue_StringValue{"XYZ11"}},
			},
		},
		ServiceConfigs: map[string]*mccpb.ServiceConfig{},
	}
	return v2
}

func GetDefaultTcpServerConf() *mccpb.TcpClientConfig {
	v2 := &mccpb.TcpClientConfig{
		MixerAttributes: &mpb.Attributes{
			Attributes: map[string]*mpb.Attributes_AttributeValue{
				"mesh1.ip":         {Value: &mpb.Attributes_AttributeValue_BytesValue{MeshIp1}},
				"target.uid":       {Value: &mpb.Attributes_AttributeValue_StringValue{"POD222"}},
				"target.namespace": {Value: &mpb.Attributes_AttributeValue_StringValue{"XYZ222"}},
			},
		},
	}
	return v2
}

func SetNetworPolicy(c *mccpb.HttpClientConfig, open bool) {
	if c.Transport == nil {
		c.Transport = &mccpb.TransportConfig{}
	}
	if open {
		c.Transport.NetworkFailPolicy = mccpb.FAIL_OPEN
	} else {
		c.Transport.NetworkFailPolicy = mccpb.FAIL_CLOSE
	}
}
