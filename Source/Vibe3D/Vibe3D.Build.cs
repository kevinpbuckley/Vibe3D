// Copyright Buckley Builds LLC 2026 All Rights Reserved.

using UnrealBuildTool;

public class Vibe3D : ModuleRules
{
	public Vibe3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.None;
		bUseUnity = false;
		bWarningsAsErrors = true;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ToolsetRegistry",          // UE 5.8 native AI toolset registry: UToolsetDefinition base + AICallable registration
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",                 // editor subsystems, UAssetImportTask, automation
				"EditorSubsystem",
				"EditorScriptingUtilities", // UEditorAssetLibrary
				"AssetRegistry",
				"AssetTools",               // texture import
				"Projects",                 // IPluginManager (plugin content dir for skill reload)
				"PythonScriptPlugin",       // Vibe3D.ReloadSkills re-runs Content/Python/init_unreal.py
				"GeometryCore",             // FDynamicMesh3 + attribute sets behind UDynamicMesh
				"GeometryFramework",        // UDynamicMesh session meshes
				"DynamicMesh",              // mesh operator types used by GeometryScript
				"GeometryScriptingCore",    // the Modeling Mode operators (primitives, booleans, remesh, UV, bake, collision, ...)
				"GeometryScriptingEditor",  // create StaticMesh / SkeletalMesh / Texture2D assets, SubD
				"StaticMeshEditor",         // UStaticMeshEditorSubsystem (LOD chain generation)
				"SkeletalMeshDescription",  // FSkeletalMeshAttributesShared (skin-weight profiles)
				"AnimationCore",            // FBoneWeights (skin weights authored on the mesh)
				"MeshDescription",
				"StaticMeshDescription",
			}
		);
	}
}
