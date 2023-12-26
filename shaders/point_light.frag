#version 450


layout (location = 0) in vec2 fragOffset;
layout (location = 0) out vec4 outColor;


struct PointLight {
	vec4 position; // ignore w
	vec4 color; // w intensity
};

const int MAX_LIGHTS = 10; //Must match value in bagel_frame_info.hpp
layout(set = 0, binding = 0) uniform GlobalUBO {
	mat4 projectionMatrix;
	mat4 viewMatrix;
	mat4 inverseViewMatrix;
	vec4 ambientLightColor;
	PointLight pointLights[MAX_LIGHTS]; //Can use 'specialization constants' to set the size of this array at pipeline creation
	int numLights;
} ubo;
layout (binding = 1) uniform sampler2D samplerColor;


const float M_PI = 3.1415926538;
const float M_E =  2.7182818284;

const float exponent = 4.4f;
const float offset = 365.0f;
//This is obviously inefficient because I can just use w of position in PointLight as a radius but for the sake of demonstrating pushcontant
layout(push_constant) uniform Push {
	vec4 position;
	vec4 color;
	float radius;
} push;

float light_exponential(float dist){
	return pow(M_E,-exponent*dist/push.radius)-exponent*0.00001*offset*dist/push.radius;
}
float light_cos(float dist){
	return pow(cos(dist * M_PI / (2*push.radius)),exponent);
}
void main(){
	float dist = sqrt(dot(fragOffset, fragOffset));
	if(dist > push.radius) {
		//Throw away this fragment and return
		//discard;
	}

	//exponential
	//outColor = vec4(push.color.xyz, light_exponential(dist));

	//cosine
	outColor = vec4(push.color.xyz+light_cos(dist), light_cos(dist));
}