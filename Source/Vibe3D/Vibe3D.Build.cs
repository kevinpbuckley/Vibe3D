// Copyright Buckley Builds LLC 2026 All Rights Reserved.

using UnrealBuildTool;

public class Vibe3D : ModuleRules
{
	public Vibe3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.None;
		bUseUnity = false;
		// Not warnings-as-errors for distribution: Epic compiles this source on their own toolchain
		// across engine patch releases, where a new engine-header deprecation would otherwise turn
		// into a hard build failure and a rejected Fab submission (VibeUE hit exactly this, issue #569).
		bWarningsAsErrors = false;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ToolsetRegistry",          // UE 5.9 native AI toolset registry: UToolsetDefinition base + AICallable registration
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",                 // editor subsystems, automation
				"EditorSubsystem",
				"EditorScriptingUtilities", // UEditorAssetLibrary
				"AssetRegistry",
				"AssetTools",               // texture import
				"Projects",                 // IPluginManager (plugin dir for the agent-guide sample)
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
