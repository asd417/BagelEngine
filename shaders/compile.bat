set GLSLC=C:\VulkanSDK\1.3.261.1\Bin\glslc.exe
set SHADERS=C:\Users\locti\OneDrive\Documents\BagelEngine\shaders

%GLSLC% %SHADERS%\wireframe_shader.vert     -o %SHADERS%\wireframe_shader.vert.spv
%GLSLC% %SHADERS%\wireframe_shader.frag     -o %SHADERS%\wireframe_shader.frag.spv
%GLSLC% %SHADERS%\gbuffer_fill.vert         -o %SHADERS%\gbuffer_fill.vert.spv
%GLSLC% %SHADERS%\gbuffer_fill.frag         -o %SHADERS%\gbuffer_fill.frag.spv
%GLSLC% %SHADERS%\planet_gbuffer.vert       -o %SHADERS%\planet_gbuffer.vert.spv
%GLSLC% %SHADERS%\planet_gbuffer.frag       -o %SHADERS%\planet_gbuffer.frag.spv
%GLSLC% %SHADERS%\deferred_lighting.vert    -o %SHADERS%\deferred_lighting.vert.spv
%GLSLC% %SHADERS%\deferred_lighting.frag    -o %SHADERS%\deferred_lighting.frag.spv
%GLSLC% %SHADERS%\radiosity.frag            -o %SHADERS%\radiosity.frag.spv
%GLSLC% %SHADERS%\transparent.vert          -o %SHADERS%\transparent.vert.spv
%GLSLC% %SHADERS%\transparent.frag          -o %SHADERS%\transparent.frag.spv
%GLSLC% %SHADERS%\water.vert                -o %SHADERS%\water.vert.spv
%GLSLC% %SHADERS%\water.frag                -o %SHADERS%\water.frag.spv
%GLSLC% %SHADERS%\bloom_downsample.frag    -o %SHADERS%\bloom_downsample.frag.spv
%GLSLC% %SHADERS%\bloom_upsample.frag      -o %SHADERS%\bloom_upsample.frag.spv
%GLSLC% %SHADERS%\shadow.vert              -o %SHADERS%\shadow.vert.spv
%GLSLC% %SHADERS%\shadow.frag              -o %SHADERS%\shadow.frag.spv
pause
