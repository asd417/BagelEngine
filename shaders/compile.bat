set GLSLC=C:\VulkanSDK\1.3.261.1\Bin\glslc.exe
set SHADERS=C:\Users\locti\OneDrive\Documents\BagelEngine\shaders

%GLSLC% %SHADERS%\simple_shader.vert        -o %SHADERS%\simple_shader.vert.spv
%GLSLC% %SHADERS%\simple_shader.frag        -o %SHADERS%\simple_shader.frag.spv
%GLSLC% %SHADERS%\wireframe_shader.vert     -o %SHADERS%\wireframe_shader.vert.spv
%GLSLC% %SHADERS%\wireframe_shader.frag     -o %SHADERS%\wireframe_shader.frag.spv
%GLSLC% %SHADERS%\point_light.vert          -o %SHADERS%\point_light.vert.spv
%GLSLC% %SHADERS%\point_light.frag          -o %SHADERS%\point_light.frag.spv
%GLSLC% %SHADERS%\gbuffer_fill.vert         -o %SHADERS%\gbuffer_fill.vert.spv
%GLSLC% %SHADERS%\gbuffer_fill.frag         -o %SHADERS%\gbuffer_fill.frag.spv
%GLSLC% %SHADERS%\deferred_lighting.vert    -o %SHADERS%\deferred_lighting.vert.spv
%GLSLC% %SHADERS%\deferred_lighting.frag    -o %SHADERS%\deferred_lighting.frag.spv
%GLSLC% %SHADERS%\transparent.frag          -o %SHADERS%\transparent.frag.spv
%GLSLC% %SHADERS%\bloom.frag               -o %SHADERS%\bloom.frag.spv
%GLSLC% %SHADERS%\bloom_downsample.frag    -o %SHADERS%\bloom_downsample.frag.spv
%GLSLC% %SHADERS%\bloom_upsample.frag      -o %SHADERS%\bloom_upsample.frag.spv
pause
