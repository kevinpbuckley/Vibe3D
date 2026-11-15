// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UModelingService.generated.h"

class UDynamicMesh;

/**
 * Outcome of a modeling operation. Handle echoes the mesh the call acted on (or the one it
 * created), TriangleCount / VertexCount describe that mesh afterwards, AssetPath is set by the
 * asset-writing calls.
 */
USTRUCT(BlueprintType)
struct FModelingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 Handle = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 VertexCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString AssetPath;
};

/** Diagnostic snapshot of a session mesh — read it between stages instead of trusting a green result. */
USTRUCT(BlueprintType)
struct FModelingMeshInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 Handle = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 VertexCount = 0;

	/** Watertight (no open border edges). Booleans and voxel ops want this true. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bIsClosed = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 OpenBorderEdges = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 ConnectedComponents = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector BoundsMin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector BoundsMax = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float SurfaceArea = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float Volume = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 NumUVLayers = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bHasVertexColors = false;

	/** Distinct material IDs in use (empty when the mesh has no material attribute). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	TArray<int32> MaterialIds;

	/** Named selections currently stored on this handle. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	TArray<FString> Selections;
};

USTRUCT(BlueprintType)
struct FModelingSplitResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	/** One new session handle per connected component, largest first. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	TArray<int32> Handles;
};

/** A point found on a mesh surface by RayCast / NearestPoint. */
USTRUCT(BlueprintType)
struct FModelingSurfacePoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bFound = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 TriangleID = -1;

	/** Ray parameter (distance along the ray) for RayCast; distance from the query point for NearestPoint. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float Distance = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector BaryCoords = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;
};

/** Surface-to-surface distances between two session meshes (MeasureDistance). */
USTRUCT(BlueprintType)
struct FModelingDistanceReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float MaxDistance = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float MinDistance = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float AverageDistance = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float RootMeanSquareDeviation = 0.f;
};

USTRUCT(BlueprintType)
struct FModelingBakeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	/** Saved Texture2D asset paths, in the order of the requested bake types. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	TArray<FString> TexturePaths;
};

/** Health of a UV layer (GetUVStats): what an unwrap looks like before you bake or paint on it. */
USTRUCT(BlueprintType)
struct FModelingUVStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	/** Connected UV islands (shells). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 NumIslands = 0;

	/** Triangles with no UVs assigned in this layer. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 NumUnsetTriangles = 0;

	/** Fraction of the 0-1 square covered by at least one island. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float Coverage = 0.f;

	/** Fraction of covered texels claimed by two or more islands (0 = no overlaps). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float OverlapFraction = 0.f;

	/** Texels per centimetre at a 1024 texture (multiply by resolution/1024). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float TexelsPerCmAt1K = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float MeshArea = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float UVArea = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector2D UVBoundsMin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector2D UVBoundsMax = FVector2D::ZeroVector;
};

/** One bone for CreateBones. Transform is in mesh space (the space the vertices are in); an empty ParentName makes it the root. */
USTRUCT(BlueprintType)
struct FModelingBoneDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString ParentName;

	/** Bone frame in mesh space. Point its X axis along a hinge line and the control surface deflects by rolling the bone. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FTransform Transform;
};

/** A bone on a session mesh: hierarchy, reference pose, and how many vertices it influences. */
USTRUCT(BlueprintType)
struct FModelingBoneInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 Index = -1;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString ParentName;

	/** Reference pose relative to the parent bone. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FTransform LocalTransform;

	/** Reference pose in mesh space. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FTransform MeshTransform;

	/** Vertices carrying a non-zero weight on this bone. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 InfluencedVertices = 0;
};

/** How far one mesh's vertices break a clearance rule against another (MeasurePenetration). */
USTRUCT(BlueprintType)
struct FModelingPenetrationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	/** Vertices close enough to the other surface (within MaxDistance, projecting onto it) to be judged. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 VerticesChecked = 0;

	/** Judged vertices on the wrong side, or closer than the margin. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 Violations = 0;

	/** Worst violation in cm (how far that vertex must move to satisfy the rule, margin included). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float MaxDepth = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	float AverageDepth = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector WorstPosition = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 WorstVertex = -1;
};

/** What IntakeGeneratedMesh did to a generated model: read it before placing the prop. */
USTRUCT(BlueprintType)
struct FModelingIntakeReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bSuccess = false;

	/** One-paragraph summary (also the failure reason). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString Message;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FString AssetPath;

	/** True when the asset already matched this file and these settings, so nothing was re-imported. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	bool bUpToDate = false;

	/** Static meshes the file produced (merged into one asset). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 SourceMeshCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 SourceTriangles = 0;

	/** LOD0 triangles after decimation. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 VertexCount = 0;

	/** Imported size before the fit, in cm (after the yaw). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector SourceSize = FVector::ZeroVector;

	/** Scale applied per axis; unequal values mean the fit stretched the model (FitAxis "Box"). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector ScaleApplied = FVector::OneVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector BoundsMin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	FVector BoundsMax = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 LODCount = 1;

	/** Simple collision shapes on the asset (0 with Collision "None"). */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	int32 CollisionShapes = 0;

	/** Material slot assets, in slot order. */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	TArray<FString> MaterialPaths;

	/** One line per texture: "<path> <source W>x<H> max <cap>". */
	UPROPERTY(BlueprintReadWrite, Category = "Vibe3D|Modeling")
	TArray<FString> Textures;
};

/**
 * Programmatic mesh modeling — the operator half of Unreal's Modeling Mode, driven without a
 * viewport. Meshes live in an in-editor session as integer handles wrapping a UDynamicMesh:
 * create or load one, chain operations on it, save it as a StaticMesh / SkeletalMesh asset,
 * release it. Operations that act on part of a mesh take a named selection made on the same
 * handle (pass "" for the whole mesh). Everything is built on GeometryScript, so the long tail
 * is one call away: GetDynamicMesh(handle) hands the UDynamicMesh to any
 * unreal.GeometryScript_* function in the same Python script.
 *
 * Python: unreal.ModelingService.<snake_case>(...). See the Vibe3D `modeling` skill for the workflow.
 */
UCLASS(BlueprintType)
class VIBE3D_API UModelingService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// =====================================================================
	// Session
	// =====================================================================

	/** Create an empty session mesh. Result.Handle is the new handle. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Create Mesh"))
	static FModelingResult CreateMesh();

	/** Load a StaticMesh asset's LOD into a new session mesh (materials become material IDs by section). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Load Mesh From Static Mesh"))
	static FModelingResult LoadMeshFromStaticMesh(const FString& AssetPath, int32 LODIndex = 0);

	/**
	 * Load a SkeletalMesh asset's LOD (with bones and bone weights) into a new session mesh. Reads the LOD's source model,
	 * the editable vertex layout that SaveMeshToSkeletalMeshLOD writes back (a LOD generated by reduction only has render
	 * data; the message says so). The handle remembers the asset and LOD it came from. A LODIndex the asset does not have
	 * fails (before the post-jam update the engine silently read the nearest LOD instead and the result claimed the one asked for).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Load Mesh From Skeletal Mesh"))
	static FModelingResult LoadMeshFromSkeletalMesh(const FString& AssetPath, int32 LODIndex = 0);

	/**
	 * Write a session mesh into ONE LOD of an existing SkeletalMesh, in place: skeleton, material slots, the other LODs,
	 * this LOD's normal/tangent settings and its section-to-slot mapping are kept. AssetPath "" and LODIndex -1 mean "where
	 * the handle was loaded from" (load_mesh_from_skeletal_mesh), so a round trip is load -> edit -> save_mesh_to_skeletal_mesh_lod(h).
	 * LODIndex == the asset's LOD count appends a LOD. Validated first, nothing written on failure: skin weights on every
	 * vertex, bones present in the skeleton (matched by name), bone and material indices in range, not a posed handle; the
	 * LOD is read back afterwards and its vertex count compared. A LOD whose build settings recompute normals keeps doing
	 * so (the handle's normals are then not used; the message says so). LODs generated from this one are regenerated from
	 * the new geometry by the engine.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Save Mesh To Skeletal Mesh LOD"))
	static FModelingResult SaveMeshToSkeletalMeshLOD(int32 Handle, const FString& AssetPath = TEXT(""), int32 LODIndex = -1, bool bSaveAsset = true);

	/** Skeleton, bone count, material slots and per-LOD source (source model or generated), vertex/triangle counts, bones used at runtime, removed bones. Read it before a LOD round trip. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Inspect Skeletal Mesh"))
	static FModelingResult InspectSkeletalMesh(const FString& AssetPath);

	/** Load the static mesh of a level actor (by label) into a new session mesh; bWorldSpace bakes the actor transform in. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Load Mesh From Actor"))
	static FModelingResult LoadMeshFromActor(const FString& ActorLabel, bool bWorldSpace = false, int32 LODIndex = 0);

	/**
	 * Read a skinned component (body, garment, accessory; leader-pose followers included) AS IT IS POSED right now, with
	 * its asset's bones and weights: the source-model vertices of that LOD, skinned by the component's current bone
	 * transforms. Looks in the running PIE world first when bPreferPlayWorld (the runtime pose, post-process and corrective
	 * bones included), else the editor world. ComponentName "" takes the first skinned component (the message lists them
	 * all). bWorldSpace false = component space. Vertex IDs match the asset LOD, and the handle keeps the pose, so after
	 * fitting edits UnposeMesh brings the result back to the reference pose for SaveMeshToSkeletalMeshLOD.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Load Mesh From Skinned Component"))
	static FModelingResult LoadMeshFromSkinnedComponent(const FString& ActorLabel, const FString& ComponentName = TEXT(""), bool bWorldSpace = false,
		int32 LODIndex = 0, bool bPreferPlayWorld = true);

	/** Undo the pose of a handle from LoadMeshFromSkinnedComponent: every vertex goes back through the inverse of its blended bone transform (with the handle's current weights), so edits made in the pose land in the reference pose. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Unpose Mesh"))
	static FModelingResult UnposeMesh(int32 Handle);

	/** Duplicate a session mesh into a new handle. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Copy Mesh"))
	static FModelingResult CopyMesh(int32 Handle);

	/** Free one session mesh (and its selections). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Release Mesh"))
	static bool ReleaseMesh(int32 Handle);

	/** Free every session mesh. Returns how many were released. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Release All Meshes"))
	static int32 ReleaseAllMeshes();

	/** Handles currently alive in the session. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "List Meshes"))
	static TArray<int32> ListMeshes();

	/** Counts, bounds, closedness, components, UV layers, material IDs, stored selections. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Get Mesh Info"))
	static FModelingMeshInfo GetMeshInfo(int32 Handle);

	/** The UDynamicMesh behind a handle, for direct use with unreal.GeometryScript_* libraries (Python only).
	 *  AIIgnore keeps it off the MCP tool list: the registry treats a static UFunction without AICallable as an error. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AIIgnore, DisplayName = "Get Dynamic Mesh"))
	static UDynamicMesh* GetDynamicMesh(int32 Handle);

	/**
	 * Every vertex position as a plain array indexed by vertex ID (no GeometryScriptVectorList to convert). The array is
	 * MaxVertexID long: a mesh with ID gaps (after deletes or booleans) has (0,0,0) in the unused slots, so its length can
	 * exceed get_mesh_info().vertex_count. Edit the list and hand it back to SetVertexPositions. Empty for an unknown handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Get Vertex Positions"))
	static TArray<FVector> GetVertexPositions(int32 Handle);

	/**
	 * Move every vertex at once: Positions[i] becomes vertex i (same layout GetVertexPositions returned; gap slots are ignored).
	 * Topology, UVs, weights and selections are untouched; normals are not recomputed (call RecomputeNormals if the shape moved a lot).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set Vertex Positions"))
	static FModelingResult SetVertexPositions(int32 Handle, const TArray<FVector>& Positions);

	// =====================================================================
	// Primitives (appended into an existing handle; Transform places them)
	// =====================================================================

	/** Origin: "Base" (sits on Z=0) or "Center". Steps subdivide the faces. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Box"))
	static FModelingResult AppendBox(int32 Handle, FTransform Transform, float DimensionX = 100.f, float DimensionY = 100.f, float DimensionZ = 100.f,
		int32 StepsX = 0, int32 StepsY = 0, int32 StepsZ = 0, const FString& Origin = TEXT("Base"), int32 MaterialID = 0);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Sphere"))
	static FModelingResult AppendSphere(int32 Handle, FTransform Transform, float Radius = 50.f, int32 StepsPhi = 10, int32 StepsTheta = 16,
		const FString& Origin = TEXT("Center"), int32 MaterialID = 0);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Cylinder"))
	static FModelingResult AppendCylinder(int32 Handle, FTransform Transform, float Radius = 50.f, float Height = 100.f, int32 RadialSteps = 12,
		int32 HeightSteps = 0, bool bCapped = true, const FString& Origin = TEXT("Base"), int32 MaterialID = 0);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Cone"))
	static FModelingResult AppendCone(int32 Handle, FTransform Transform, float BaseRadius = 50.f, float TopRadius = 5.f, float Height = 100.f,
		int32 RadialSteps = 12, int32 HeightSteps = 4, bool bCapped = true, const FString& Origin = TEXT("Base"), int32 MaterialID = 0);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Capsule"))
	static FModelingResult AppendCapsule(int32 Handle, FTransform Transform, float Radius = 30.f, float LineLength = 75.f, int32 HemisphereSteps = 5,
		int32 CircleSteps = 8, const FString& Origin = TEXT("Base"), int32 MaterialID = 0);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Torus"))
	static FModelingResult AppendTorus(int32 Handle, FTransform Transform, float MajorRadius = 50.f, float MinorRadius = 25.f, int32 MajorSteps = 16,
		int32 MinorSteps = 8, const FString& Origin = TEXT("Base"), int32 MaterialID = 0);

	/** Flat rectangle in the XY plane. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Rectangle"))
	static FModelingResult AppendRectangle(int32 Handle, FTransform Transform, float DimensionX = 100.f, float DimensionY = 100.f, int32 StepsWidth = 0,
		int32 StepsHeight = 0, int32 MaterialID = 0);

	/** Flat disc / ring / arc in the XY plane. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Disc"))
	static FModelingResult AppendDisc(int32 Handle, FTransform Transform, float Radius = 50.f, int32 AngleSteps = 16, int32 SpokeSteps = 0,
		float StartAngle = 0.f, float EndAngle = 360.f, float HoleRadius = 0.f, int32 MaterialID = 0);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Stairs"))
	static FModelingResult AppendStairs(int32 Handle, FTransform Transform, float StepWidth = 100.f, float StepHeight = 20.f, float StepDepth = 30.f,
		int32 NumSteps = 8, bool bFloating = false, int32 MaterialID = 0);

	/** Extrude a closed 2D polygon (XY, counter-clockwise) along +Z. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Extrude Polygon"))
	static FModelingResult AppendExtrudePolygon(int32 Handle, FTransform Transform, const TArray<FVector2D>& PolygonPoints, float Height = 100.f,
		int32 HeightSteps = 0, bool bCapped = true, const FString& Origin = TEXT("Base"), int32 MaterialID = 0);

	/** Revolve a 2D profile (X = radius offset, Y = height) around the Z axis: lathe shapes, rings, vases. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Revolve Polygon"))
	static FModelingResult AppendRevolvePolygon(int32 Handle, FTransform Transform, const TArray<FVector2D>& ProfilePoints, float Radius = 100.f,
		int32 Steps = 16, float RevolveDegrees = 360.f, int32 MaterialID = 0);

	/** Append another session mesh (transformed) into this one — no boolean, just merged geometry. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Mesh"))
	static FModelingResult AppendMesh(int32 Handle, int32 OtherHandle, FTransform Transform);

	// =====================================================================
	// Booleans
	// =====================================================================

	/** Operation: "Union", "Subtract", "Intersection". ToolTransform places the tool mesh relative to the target. Both should be closed. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Boolean"))
	static FModelingResult Boolean(int32 TargetHandle, int32 ToolHandle, const FString& Operation, FTransform ToolTransform,
		bool bFillHoles = true, bool bSimplifyOutput = true);

	/** Union a mesh with itself — merges overlapping kitbash pieces into one shell. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Self Union"))
	static FModelingResult SelfUnion(int32 Handle, bool bFillHoles = true, bool bTrimFlaps = true);

	/** Cut away everything on the +Z side of CutFrame (its Z axis is the plane normal). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Plane Cut"))
	static FModelingResult PlaneCut(int32 Handle, FTransform CutFrame, bool bFillHoles = true, bool bFlipCutSide = false);

	/** Mirror across MirrorFrame's XY plane (optionally cutting the source side first and welding the seam). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Mirror"))
	static FModelingResult Mirror(int32 Handle, FTransform MirrorFrame, bool bApplyPlaneCut = true, bool bWeldAlongPlane = true, bool bFlipCutSide = false);

	// =====================================================================
	// Selections (named, stored per handle; "" means the whole mesh)
	// =====================================================================

	/** SelectionType: "Triangles" (default), "Vertices", "Edges", "Polygroups". Returns the element count. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select All"))
	static int32 SelectAll(int32 Handle, const FString& SelectionName, const FString& SelectionType = TEXT("Triangles"));

	/** Triangles whose normal is within MaxAngleDeg of Normal (e.g. (0,0,1) + 5 = the top faces). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select By Normal Angle"))
	static int32 SelectByNormalAngle(int32 Handle, const FString& SelectionName, FVector Normal, float MaxAngleDeg = 1.f, bool bInvert = false);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select In Box"))
	static int32 SelectInBox(int32 Handle, const FString& SelectionName, FVector BoxMin, FVector BoxMax, bool bInvert = false);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select In Sphere"))
	static int32 SelectInSphere(int32 Handle, const FString& SelectionName, FVector Center, float Radius = 100.f, bool bInvert = false);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select By Material ID"))
	static int32 SelectByMaterialID(int32 Handle, const FString& SelectionName, int32 MaterialID);

	/** Grow (or shrink with bContract) a selection by N rings of neighbours; stores it under NewSelectionName. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Expand Contract Selection"))
	static int32 ExpandContractSelection(int32 Handle, const FString& SelectionName, const FString& NewSelectionName, int32 Iterations = 1, bool bContract = false);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Invert Selection"))
	static int32 InvertSelection(int32 Handle, const FString& SelectionName, const FString& NewSelectionName);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Selection Count"))
	static int32 SelectionCount(int32 Handle, const FString& SelectionName);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Clear Selections"))
	static bool ClearSelections(int32 Handle);

	// =====================================================================
	// Poly-edit operations (Modeling Mode's PolyEdit / Offset / Bevel)
	// =====================================================================

	/** Extrude selected faces. Zero Direction = along each face's average normal; otherwise a fixed world direction. Negative distance recesses. The selection then holds the moved faces. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Extrude Faces"))
	static FModelingResult ExtrudeFaces(int32 Handle, const FString& SelectionName, float Distance = 10.f, FVector Direction = FVector::ZeroVector);

	/** Offset selected faces along their normals (parallel face offset). The selection then holds the moved faces. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Offset Faces"))
	static FModelingResult OffsetFaces(int32 Handle, const FString& SelectionName, float Distance = 10.f);

	/** Inset selected faces (panel lines, frames). Softness rounds the inset boundary. The selection then holds the inner faces, ready to extrude. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Inset Faces"))
	static FModelingResult InsetFaces(int32 Handle, const FString& SelectionName, float Distance = 5.f, float Softness = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Outset Faces"))
	static FModelingResult OutsetFaces(int32 Handle, const FString& SelectionName, float Distance = 5.f, float Softness = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Delete Faces"))
	static FModelingResult DeleteFaces(int32 Handle, const FString& SelectionName);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Translate Selection"))
	static FModelingResult TranslateSelection(int32 Handle, const FString& SelectionName, FVector Delta);

	/** Bevel the edges between polygroups (run ComputePolygroups first on imported meshes). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Bevel Polygroups"))
	static FModelingResult BevelPolygroups(int32 Handle, float Distance = 1.f, int32 Subdivisions = 0, float RoundWeight = 1.f);

	/** Offset the whole surface along its normals (inflate / deflate). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Offset Mesh"))
	static FModelingResult OffsetMesh(int32 Handle, float Distance = 1.f);

	/** Turn an open surface into a solid shell of the given thickness. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Shell Mesh"))
	static FModelingResult ShellMesh(int32 Handle, float Thickness = 1.f);

	// =====================================================================
	// Mesh processing
	// =====================================================================

	/** Uniform remesh to a triangle count, or to an edge length when EdgeLength > 0. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Remesh"))
	static FModelingResult Remesh(int32 Handle, int32 TargetTriangleCount = 5000, float EdgeLength = 0.f, bool bReprojectToInput = true);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Simplify To Triangle Count"))
	static FModelingResult SimplifyToTriangleCount(int32 Handle, int32 TriangleCount, bool bAllowSeamCollapse = true);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Simplify To Tolerance"))
	static FModelingResult SimplifyToTolerance(int32 Handle, float Tolerance = 0.5f, bool bAllowSeamCollapse = true);

	/** Method: "PN" (curved, smooth normals), "Uniform" (flat split), "CatmullClark" (polygroup SubD), "Loop" (triangle SubD). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Subdivide"))
	static FModelingResult Subdivide(int32 Handle, int32 Level = 1, const FString& Method = TEXT("PN"));

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Smooth"))
	static FModelingResult Smooth(int32 Handle, const FString& SelectionName, int32 Iterations = 5, float Alpha = 0.2f);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Fill Holes"))
	static FModelingResult FillHoles(int32 Handle);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Weld Edges"))
	static FModelingResult WeldEdges(int32 Handle, float Tolerance = 0.001f);

	/**
	 * Repair degenerate triangles, drop tiny floating pieces, compact — the standard cleanup after
	 * imports and booleans. Collapsing degenerates can punch holes in a watertight mesh, so with
	 * bKeepClosed (default) a mesh that was closed on the way in is re-closed on the way out;
	 * pass false if you want the raw result.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Repair"))
	static FModelingResult Repair(int32 Handle, float MinComponentVolume = 0.0001f, int32 MinComponentTriangles = 1, bool bKeepClosed = true);

	/** Remove triangles that cannot be seen from outside (kitbash interiors). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Remove Hidden Triangles"))
	static FModelingResult RemoveHiddenTriangles(int32 Handle);

	/** Split into connected components, each becoming a new session handle. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Split By Components"))
	static FModelingSplitResult SplitByComponents(int32 Handle);

	// =====================================================================
	// Deformation
	// =====================================================================

	/** Bend around the frame's Y axis by Angle degrees within Extent of its origin. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Bend"))
	static FModelingResult Bend(int32 Handle, FTransform Orientation, float AngleDeg = 45.f, float Extent = 50.f, bool bBidirectional = true);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Twist"))
	static FModelingResult Twist(int32 Handle, FTransform Orientation, float AngleDeg = 45.f, float Extent = 50.f, bool bBidirectional = true);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Flare"))
	static FModelingResult Flare(int32 Handle, FTransform Orientation, float PercentX = 20.f, float PercentY = 20.f, float Extent = 50.f);

	/** Perlin displacement along normals (rock, damage, organic wobble). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Noise"))
	static FModelingResult Noise(int32 Handle, const FString& SelectionName, float Magnitude = 5.f, float Frequency = 0.25f, int32 RandomSeed = 0);

	/** Displace along normals by a texture sampled through a UV layer (height maps). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Displace From Texture"))
	static FModelingResult DisplaceFromTexture(int32 Handle, const FString& SelectionName, const FString& TexturePath, float Magnitude = 10.f, int32 UVLayer = 0);

	// =====================================================================
	// Voxel operations
	// =====================================================================

	/** Rebuild as a watertight voxel-derived surface (fixes any topology; loses UVs). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Voxel Solidify"))
	static FModelingResult VoxelSolidify(int32 Handle, int32 GridResolution = 64, float WindingThreshold = 0.5f);

	/** Operation: "Dilate", "Contract", "Open", "Close" by Distance (Close welds nearby pieces, Open removes thin bits). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Voxel Morphology"))
	static FModelingResult VoxelMorphology(int32 Handle, const FString& Operation, float Distance = 5.f, int32 GridResolution = 64);

	// =====================================================================
	// UVs, normals, groups, materials, colors
	// =====================================================================

	/** Method: "XAtlas" (general unwrap) or "PatchBuilder" (hard-surface, respects groups). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Auto UV"))
	static FModelingResult AutoUV(int32 Handle, const FString& Method = TEXT("XAtlas"), int32 UVLayer = 0);

	/** Method: "Planar", "Box", "Cylinder"; the transform positions/scales the projector. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Project UV"))
	static FModelingResult ProjectUV(int32 Handle, const FString& Method, FTransform ProjectorTransform, int32 UVLayer = 0, const FString& SelectionName = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Repack UV"))
	static FModelingResult RepackUV(int32 Handle, int32 UVLayer = 0, int32 TextureResolution = 1024);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set Num UV Layers"))
	static FModelingResult SetNumUVLayers(int32 Handle, int32 NumLayers);

	/** HardAngleDeg < 0 = fully smooth; otherwise edges sharper than the angle get split normals. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Recompute Normals"))
	static FModelingResult RecomputeNormals(int32 Handle, float HardAngleDeg = 30.f);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Flip Normals"))
	static FModelingResult FlipNormals(int32 Handle);

	/** Assign polygroups. Method: "Angle" (crease angle), "UVIslands", "Components", "Polygons" (detect quads/planar polygons). Needed by BevelPolygroups / PatchBuilder / SelectByPolygroup on imported meshes. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Compute Polygroups"))
	static FModelingResult ComputePolygroups(int32 Handle, const FString& Method = TEXT("Angle"), float CreaseAngleDeg = 15.f, int32 MinGroupSize = 2);

	/** Move every triangle with FromMaterialID to ToMaterialID. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Remap Material ID"))
	static FModelingResult RemapMaterialID(int32 Handle, int32 FromMaterialID, int32 ToMaterialID);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set Material ID"))
	static FModelingResult SetMaterialID(int32 Handle, const FString& SelectionName, int32 MaterialID);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set Vertex Color"))
	static FModelingResult SetVertexColor(int32 Handle, const FString& SelectionName, FLinearColor Color);

	// =====================================================================
	// Transform
	// =====================================================================

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Transform Mesh"))
	static FModelingResult TransformMesh(int32 Handle, FTransform Transform);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Translate Mesh"))
	static FModelingResult TranslateMesh(int32 Handle, FVector Translation);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Rotate Mesh"))
	static FModelingResult RotateMesh(int32 Handle, FRotator Rotation, FVector Origin = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Scale Mesh"))
	static FModelingResult ScaleMesh(int32 Handle, FVector Scale, FVector Origin = FVector::ZeroVector);

	/** Mode: "Bounds" (bounds center to origin) or "Base" (XY center to origin, bottom to Z=0). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Recenter Mesh"))
	static FModelingResult RecenterMesh(int32 Handle, const FString& Mode = TEXT("Base"));

	// =====================================================================
	// Assets, collision, LODs, baking, placement
	// =====================================================================

	/** Write the mesh to a StaticMesh asset (created, or LOD0 replaced when it exists and bReplaceExisting). Saves the package. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Save Mesh To Static Mesh"))
	static FModelingResult SaveMeshToStaticMesh(int32 Handle, const FString& AssetPath, bool bReplaceExisting = true, bool bEnableCollision = true,
		bool bEnableNanite = false, bool bSaveAsset = true);

	/**
	 * Write the mesh (with its bone weights) to a SkeletalMesh asset. SkeletonPath "" creates a new Skeleton asset at
	 * <AssetPath>_Skeleton from the bones on the mesh (CreateBones); an existing skeleton is bound as-is. One material slot
	 * is made per material ID — assign materials afterwards with SetAssetMaterials.
	 * When the asset already exists (and bReplaceExisting), LOD0 is replaced in place through the same validated writer as
	 * SaveMeshToSkeletalMeshLOD: skeleton, material slots, which section uses which slot and the other LODs are kept, bones
	 * are matched by name, and the handle's normals and tangents are stored as they are. Refused, with nothing written
	 * (these used to write a broken asset): vertices without skin weight, bones the asset's skeleton lacks, a material ID
	 * with no slot, a posed handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Save Mesh To Skeletal Mesh"))
	static FModelingResult SaveMeshToSkeletalMesh(int32 Handle, const FString& AssetPath, const FString& SkeletonPath = TEXT(""), bool bReplaceExisting = true, bool bSaveAsset = true);

	/** Assign material slots on a StaticMesh or SkeletalMesh asset: comma-separated material asset paths, in slot order. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set Asset Materials"))
	static FModelingResult SetAssetMaterials(const FString& AssetPath, const FString& MaterialPaths, bool bSaveAsset = true);

	/** Copy skin weights from a SkeletalMesh asset onto this mesh by closest surface point (re-skin a modified body). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Transfer Bone Weights"))
	static FModelingResult TransferBoneWeights(int32 Handle, const FString& SourceSkeletalMeshPath, int32 SourceLODIndex = 0);

	/** Method: "MinVolumeShapes", "ConvexHulls", "AlignedBoxes", "OrientedBoxes", "MinimalSpheres", "Capsules", "SweptHulls". Acts on the asset. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Generate Collision"))
	static FModelingResult GenerateCollision(const FString& AssetPath, const FString& Method = TEXT("MinVolumeShapes"), int32 MaxConvexHulls = 1,
		int32 ConvexHullTargetFaceCount = 25, bool bSaveAsset = true);

	/** Rebuild the asset's LOD chain: one entry per LOD as a fraction of LOD0 triangles (LOD0 first, e.g. [1.0, 0.5, 0.25]). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set LODs", ScriptName = "set_lods;set_lo_ds"))
	static FModelingResult SetLODs(const FString& AssetPath, const TArray<float>& PercentTrianglesPerLOD, bool bAutoComputeScreenSize = true, bool bSaveAsset = true);

	/**
	 * Generated-prop intake in one call: import a model file from an image-to-3D service (GLB/glTF, also FBX/OBJ), merge its
	 * meshes into ONE StaticMesh at AssetPath, turn it by YawDeg about Z, scale it to real-world size, move the pivot, decimate
	 * to MaxTriangles, cap its textures, build LODs and collision, and report what it did. Materials and textures land next to
	 * the mesh as MI_<Name>_<i> / T_<Name>_<Role>; nothing else from the import is kept.
	 * FitAxis: "Height" (Z extent = TargetSizeCm), "Width" (X), "Depth" (Y), "Longest", "Box" (stretch to TargetBoxCm, per axis),
	 * "None". Pivot: "BottomCenter", "Center", "TopCenter" (hanging lamps), "None". Collision: "ConvexHulls" (MaxConvexHulls
	 * pieces), "Box", "None", or any GenerateCollision method. LODPercents: comma list of LOD0 fractions ("1,0.5,0.25"; "" = one
	 * LOD). Re-running with the same file and settings is a no-op (bUpToDate) unless bForce; a changed file or setting replaces
	 * the asset in place, so placed actors keep working. A StaticMesh at AssetPath that intake did not make is only replaced
	 * with bForce. MaxTextureSize sets each texture's "Maximum Texture Size" (what is built, cooked and loaded); the source
	 * pixels in the .uasset stay at full resolution.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Intake Generated Mesh"))
	static FModelingIntakeReport IntakeGeneratedMesh(const FString& SourceFile, const FString& AssetPath, float TargetSizeCm = 100.f,
		const FString& FitAxis = TEXT("Height"), float YawDeg = 0.f, const FString& Pivot = TEXT("BottomCenter"), int32 MaxTriangles = 20000,
		int32 MaxTextureSize = 1024, const FString& Collision = TEXT("ConvexHulls"), int32 MaxConvexHulls = 4, const FString& LODPercents = TEXT(""),
		FVector TargetBoxCm = FVector::ZeroVector, bool bEnableNanite = false, bool bForce = false, bool bSaveAsset = true);

	/** Bake maps from SourceHandle onto TargetHandle's UVs. BakeTypes: comma list of "TangentNormal", "ObjectNormal", "AmbientOcclusion", "Curvature", "Position", "BentNormal". Resolution 16..8192 (power of two). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Bake Textures"))
	static FModelingBakeResult BakeTextures(int32 TargetHandle, int32 SourceHandle, const FString& BakeTypes, int32 Resolution, const FString& OutputFolder,
		const FString& BaseName, int32 TargetUVLayer = 0, int32 SamplesPerPixel = 1);

	// =====================================================================
	// More primitives
	// =====================================================================

	/** Spiral / curved staircase around the +Z axis. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Curved Stairs"))
	static FModelingResult AppendCurvedStairs(int32 Handle, FTransform Transform, float StepWidth = 100.f, float StepHeight = 20.f, float InnerRadius = 150.f,
		float CurveAngle = 90.f, int32 NumSteps = 8, bool bFloating = false, int32 MaterialID = 0);

	/** Sphere built from a subdivided cube (even quads — better for booleans and UVs than a lat-long sphere). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Sphere Box"))
	static FModelingResult AppendSphereBox(int32 Handle, FTransform Transform, float Radius = 50.f, int32 Steps = 6, const FString& Origin = TEXT("Center"), int32 MaterialID = 0);

	/** Sweep an OPEN 2D polyline along a path of transforms: rails, trim, ribbons, gutters. The profile lies in each frame's local YZ plane and is scaled by the frame's scale; bLoop closes the PATH (last frame back to the first), not the profile. For closed, capped solids use AppendLoft. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Sweep Polyline"))
	static FModelingResult AppendSweepPolyline(int32 Handle, FTransform Transform, const TArray<FVector2D>& ProfilePoints, const TArray<FTransform>& SweepPath,
		bool bLoop = false, float StartScale = 1.f, float EndScale = 1.f, int32 MaterialID = 0);

	// =====================================================================
	// More simplification
	// =====================================================================

	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Simplify To Vertex Count"))
	static FModelingResult SimplifyToVertexCount(int32 Handle, int32 VertexCount, bool bAllowSeamCollapse = true);

	/** Collapse coplanar triangles only — lossless cleanup for boolean / voxel output. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Simplify Planar"))
	static FModelingResult SimplifyPlanar(int32 Handle, float AngleThresholdDeg = 0.001f);

	// =====================================================================
	// Spatial queries and sampling
	// =====================================================================

	/** First hit of a ray against the mesh (MaxDistance 0 = unlimited). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Ray Cast"))
	static FModelingSurfacePoint RayCast(int32 Handle, FVector Origin, FVector Direction, float MaxDistance = 0.f);

	/** Closest point on the surface to a query point (MaxDistance 0 = unlimited). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Nearest Point"))
	static FModelingSurfacePoint NearestPoint(int32 Handle, FVector Point, float MaxDistance = 0.f);

	/** Winding-number containment test — the mesh should be closed. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Is Point Inside"))
	static bool IsPointInside(int32 Handle, FVector Point);

	/** Poisson-disc samples on the surface (position + surface-aligned rotation), for scattering props/foliage/decals. MaxSamples 0 = no cap. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Sample Surface Points"))
	static TArray<FTransform> SampleSurfacePoints(int32 Handle, float SamplingRadius = 10.f, int32 MaxSamples = 0);

	/** Bounding box of a selection. Returns false when the selection is empty. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Selection Bounds"))
	static bool SelectionBounds(int32 Handle, const FString& SelectionName, FVector& OutMin, FVector& OutMax);

	/** Select every triangle in one polygroup (see ComputePolygroups / GetMeshInfo). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select By Polygroup"))
	static int32 SelectByPolygroup(int32 Handle, const FString& SelectionName, int32 PolygroupID);

	// =====================================================================
	// Hulls and comparison
	// =====================================================================

	/** Convex hull of the mesh as a NEW handle (SimplifyToFaceCount 0 = exact). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Convex Hull"))
	static FModelingResult ConvexHull(int32 Handle, int32 SimplifyToFaceCount = 0);

	/** Approximate the mesh with NumHulls convex pieces, returned merged as a NEW handle (collision proxies, blockout simplification). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Convex Decomposition"))
	static FModelingResult ConvexDecomposition(int32 Handle, int32 NumHulls = 2);

	/** Hull swept along ProjectionFrame's Z axis (2D outline extruded through the mesh) as a NEW handle. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Swept Hull"))
	static FModelingResult SweptHull(int32 Handle, FTransform ProjectionFrame);

	// =====================================================================
	// Fit one mesh against another (garments on bodies, layers of clothing). Both handles must be in the same space.
	// A vertex is judged only when it lies within MaxDistance of the other surface AND projects onto it (the offset to the
	// nearest point runs along that face's normal), so a head is never dragged into a collar. The other mesh's face
	// normals say which side is "outside".
	// =====================================================================

	/** Report how far Handle's vertices break the rule "stay on Side of OtherHandle by Margin cm". Side "Inside" (skin under a garment) or "Outside" (a garment over skin). OutSelectionName (optional) stores the offending vertices. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Measure Penetration"))
	static FModelingPenetrationReport MeasurePenetration(int32 Handle, int32 OtherHandle, const FString& Side = TEXT("Inside"), float Margin = 0.f, float MaxDistance = 5.f,
		const FString& SelectionName = TEXT(""), const FString& OutSelectionName = TEXT(""));

	/** Move Handle's vertices that poke out through OtherHandle (or sit closer than Margin under it) inward along the other surface's normal until Margin under it — skin pushed under clothes. SmoothIterations feathers the move into that many rings of neighbours so no crease shows. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Push Inside"))
	static FModelingResult PushInside(int32 Handle, int32 OtherHandle, float Margin = 0.3f, const FString& SelectionName = TEXT(""), float MaxDistance = 5.f, int32 SmoothIterations = 2);

	/** Move Handle's vertices that sink into OtherHandle (or sit closer than Margin above it) outward along the other surface's normal until Margin over it — a garment pushed off the body or off the face. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Push Outside"))
	static FModelingResult PushOutside(int32 Handle, int32 OtherHandle, float Margin = 0.3f, const FString& SelectionName = TEXT(""), float MaxDistance = 5.f, int32 SmoothIterations = 2);

	/** Select Handle's triangles hidden under OtherHandle: every vertex (or any, with bAllVertices false) at least Margin inside it and covered by it. delete_faces on the result drops skin under clothes or an inner garment under an outer one. Returns the triangle count. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select Hidden By"))
	static int32 SelectHiddenBy(int32 Handle, const FString& SelectionName, int32 OtherHandle, float Margin = 0.5f, float MaxDistance = 5.f, bool bAllVertices = true);

	/** Surface distances between two meshes — verify a LOD / simplified copy against its source. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Measure Distance"))
	static FModelingDistanceReport MeasureDistance(int32 HandleA, int32 HandleB);

	// =====================================================================
	// More skeletal
	// =====================================================================

	/** Recompute smooth skin weights against a skeleton after transfer or edits. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Smooth Bone Weights"))
	static FModelingResult SmoothBoneWeights(int32 Handle, const FString& SkeletonPath, float Stiffness = 0.2f, int32 MaxInfluences = 5);

	/** Remove bones (comma-separated names) from the skin weights, renormalizing the rest. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Prune Bone Weights"))
	static FModelingResult PruneBoneWeights(int32 Handle, const FString& BoneNames);

	/** Select every vertex where the listed bones (comma-separated) together hold at least MinWeight of the skinning — the region a bone actually drags. Returns the vertex count. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select By Bone Weight"))
	static int32 SelectByBoneWeight(int32 Handle, const FString& SelectionName, const FString& BoneNames, float MinWeight = 0.25f);

	/**
	 * Scale the listed bones' influence on a selection ("" = whole mesh): Scale 0 removes it, 0.25 keeps a quarter. The freed
	 * weight goes to ReceiverBoneName when given, otherwise to each vertex's strongest other influence. The targeted fix for
	 * clothing that follows the wrong bone — a collar lifting with the head, a skirt hem glued to a thigh. Vertices bound
	 * only to the listed bones are skipped unless a receiver is named.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Scale Bone Weights"))
	static FModelingResult ScaleBoneWeights(int32 Handle, const FString& SelectionName, const FString& BoneNames, float Scale = 0.f, const FString& ReceiverBoneName = TEXT(""));

	/** Laplacian-smooth skin weights across a selection ("" = whole mesh) — soften the seam after BindSelectionToBone or ScaleBoneWeights so a rigid patch blends into its surroundings. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Smooth Selection Bone Weights"))
	static FModelingResult SmoothSelectionBoneWeights(int32 Handle, const FString& SelectionName, int32 Iterations = 3, float Alpha = 0.5f);

	/**
	 * Skin a mesh (typically a static accessory: a tie, dog tags, a holster) to the surface it sits on, so it follows that
	 * surface instead of riding rigidly on one bone. SourceHandle is a skinned session mesh in the SAME space as Handle
	 * (load_mesh_from_skeletal_mesh for the reference pose, load_mesh_from_skinned_component for the pose as displayed).
	 * Mode: "Surface" (weights of the closest surface point), "Inpaint" (closest point where the match is clear, smoothly
	 * filled elsewhere: better for pieces that stand off the surface), "Rigid" (every vertex 100% on RigidBoneName, or on
	 * the bone the surface under it is most weighted to). The result carries the source's bones, or those of
	 * BonesFromSkeletalMesh (e.g. the body the accessory will follow as a leader-pose follower; weights are matched by name).
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Skin To Surface"))
	static FModelingResult SkinToSurface(int32 Handle, int32 SourceHandle, const FString& Mode = TEXT("Surface"), const FString& RigidBoneName = TEXT(""),
		const FString& BonesFromSkeletalMesh = TEXT(""), int32 SmoothIterations = 0, float SmoothStrength = 0.f);

	/**
	 * Fix a skinned follower (accessory, garment) that streaks at lower LODs: for each of its LODs, every bone the reference
	 * mesh (the leader, usually the body) does not keep at that LOD hands its weight to its nearest kept ancestor, then the
	 * LOD is written back in place. The follower's last LOD also covers the reference's lower LODs. Reports, per LOD, which
	 * bones were remapped and how many vertices moved. A changed LOD that was generated by reduction becomes a custom LOD.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Remap LOD Bones"))
	static FModelingResult RemapLODBones(const FString& AssetPath, const FString& ReferenceSkeletalMeshPath, bool bSaveAsset = true);

	/** Per-bone skin weight report over a selection ("" = whole mesh): influenced vertices, average/max weight per bone, weight-sum range, max influences. Verify a repair before saving. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Inspect Bone Weights"))
	static FModelingResult InspectBoneWeights(int32 Handle, const FString& SelectionName = TEXT(""));

	// =====================================================================
	// Lofts, orientation, connected pieces
	// =====================================================================

	/**
	 * Solid made by sweeping a closed 2D profile through a list of frames and capping both ends — wings, fins, hulls,
	 * tapered beams. The profile lies in each frame's local YZ plane (profile X along the frame's Y axis, profile Y along
	 * its Z axis) and is multiplied by the frame's scale, so chord and taper go in Frames[i].Scale. A wing running out
	 * along +Y with its chord toward -X: root and tip frames with rotation (roll 0, pitch 0, yaw 90). The result is always
	 * closed and outward-facing regardless of the winding the sweep produced.
	 */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Loft"))
	static FModelingResult AppendLoft(int32 Handle, FTransform Transform, const TArray<FVector2D>& ProfilePoints, const TArray<FTransform>& Frames, int32 MaterialID = 0);

	/** Flip the mesh when its enclosed volume is negative (normals facing inward). Run on any closed part before booleans — an inside-out part is silently discarded by SelfUnion. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Ensure Outward"))
	static FModelingResult EnsureOutward(int32 Handle);

	/** Select the whole connected piece of geometry nearest to Point — a control surface isolated by slot cuts, a bolt on a plate. Returns the triangle count. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Select Connected"))
	static int32 SelectConnected(int32 Handle, const FString& SelectionName, FVector Point);

	// =====================================================================
	// Rigging: bones and skin weights authored on the mesh itself
	// =====================================================================

	/** Replace the mesh's bone hierarchy. The first bone is the root; every other bone names a parent listed before it. Every vertex starts bound to the root. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Create Bones"))
	static FModelingResult CreateBones(int32 Handle, const TArray<FModelingBoneDef>& Bones);

	/** Bind the vertices of a selection ("" = all) to a bone. Weight 1 replaces their weights; a lower weight blends with what is there. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Bind Selection To Bone"))
	static FModelingResult BindSelectionToBone(int32 Handle, const FString& SelectionName, const FString& BoneName, float Weight = 1.f);

	/** Bones on the mesh with their reference poses and influenced-vertex counts — check a rig before saving it. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "List Bones"))
	static TArray<FModelingBoneInfo> ListBones(int32 Handle);

	// =====================================================================
	// UV control: islands you define, layouts you can verify
	// =====================================================================

	/** Give every triangle of a selection one polygroup: GroupID >= 0 sets that id, -1 allocates a new group. Polygroups are the island source for RecomputeUVs("Polygroups") and PatchBuilder. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Set Polygroup"))
	static FModelingResult SetPolygroup(int32 Handle, const FString& SelectionName, int32 GroupID = -1);

	/** Flatten each island with a conformal unwrap. IslandSource: "Polygroups" (one island per group — define them with SetPolygroup / ComputePolygroups) or "UVIslands" (re-flatten the existing islands). Method: "SpectralConformal" (default), "Conformal", "ExpMap". */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Recompute UVs", ScriptName = "recompute_uvs;recompute_u_vs"))
	static FModelingResult RecomputeUVs(int32 Handle, int32 UVLayer = 0, const FString& IslandSource = TEXT("Polygroups"), const FString& Method = TEXT("SpectralConformal"),
		const FString& SelectionName = TEXT(""), bool bAlignIslandsWithAxes = true);

	/** Move / scale / rotate the UVs of a selection ("" = all) in UV space: scale and rotation are about Origin (0.5,0.5 = texture centre). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Transform UV", ScriptName = "transform_uv"))
	static FModelingResult TransformUV(int32 Handle, int32 UVLayer, const FString& SelectionName, FVector2D Translation, FVector2D Scale, float RotationDeg = 0.f, FVector2D Origin = FVector2D(0.5, 0.5));

	/** LayoutType: "Repack" (pack islands into 0-1 for TextureResolution), "Stack" (all islands on top of each other), "Normalize" (scale to unit texel density), "Transform" (Scale + Translation). Works on a selection's islands. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Layout UV", ScriptName = "layout_uv"))
	static FModelingResult LayoutUV(int32 Handle, int32 UVLayer = 0, const FString& LayoutType = TEXT("Repack"), const FString& SelectionName = TEXT(""), int32 TextureResolution = 2048,
		float Scale = 1.f, FVector2D Translation = FVector2D::ZeroVector);

	/** Pack the islands of each material ID into their own full 0-1 range so every material slot gets its own texture set at full resolution. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Pack UV Per Material", ScriptName = "pack_uv_per_material"))
	static FModelingResult PackUVPerMaterial(int32 Handle, int32 UVLayer = 0, int32 TextureResolution = 2048);

	/** Islands, coverage, overlap and texel density of a UV layer — check an unwrap before baking or painting. GridResolution is the raster used for coverage/overlap. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Get UV Stats", ScriptName = "get_uv_stats"))
	static FModelingUVStats GetUVStats(int32 Handle, int32 UVLayer = 0, int32 GridResolution = 512);

	/** UV coordinate of the surface point nearest to a mesh-space position (where a marking lands on the texture). Returns false when the mesh has no UVs there. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "World To UV", ScriptName = "world_to_uv"))
	static bool WorldToUV(int32 Handle, FVector Point, int32 UVLayer, FVector2D& OutUV);

	// =====================================================================
	// More bakes and image tools
	// =====================================================================

	/** Transfer textures from SourceHandle's UVs onto TargetHandle's: one path = a plain texture bake, several (comma-separated, in source material-ID order) = a multi-texture bake. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Bake Texture Transfer"))
	static FModelingBakeResult BakeTextureTransfer(int32 TargetHandle, int32 SourceHandle, const FString& SourceTexturePaths, int32 Resolution, const FString& OutputFolder,
		const FString& BaseName, int32 TargetUVLayer = 0, int32 SourceUVLayer = 0, int32 SamplesPerPixel = 1);

	/** Tileable fractal (Perlin fBm) grayscale texture — grunge, wear masks, height detail. Scale is the number of noise cells across the image. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Create Noise Texture"))
	static FModelingResult CreateNoiseTexture(const FString& AssetPath, int32 Width = 1024, int32 Height = 1024, float Scale = 8.f, int32 Octaves = 4, float Persistence = 0.5f,
		int32 Seed = 0, bool bSaveAsset = true);

	/** Draw into an existing 8-bit texture in UV space (0-1, V down). Shape: "Line" (polyline through Points), "Dots" (discs every SpacingPx along the polyline), "Rect" (Points[0]..Points[1] filled), "Fill" (whole texture). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Draw On Texture"))
	static FModelingResult DrawOnTexture(const FString& AssetPath, const FString& Shape, const TArray<FVector2D>& Points, FLinearColor Color, float ThicknessPx = 2.f,
		float SpacingPx = 8.f, bool bSaveAsset = true);

	// =====================================================================
	// Surface detail helpers
	// =====================================================================

	/** Stamp another session mesh at every transform (pair with SampleSurfacePoints for scattered rivets, bolts, fasteners). */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Mesh At Transforms"))
	static FModelingResult AppendMeshAtTransforms(int32 Handle, int32 OtherHandle, const TArray<FTransform>& Transforms);

	/** Stamp another session mesh every Spacing along a polyline, X axis along the line and Z along UpVector — rivet rows, fastener lines, light strips. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Append Mesh Along Polyline"))
	static FModelingResult AppendMeshAlongPolyline(int32 Handle, int32 OtherHandle, const TArray<FVector>& Points, float Spacing = 10.f, FVector UpVector = FVector::UpVector, float Scale = 1.f);

	/** Cut a rectangular groove (Width across, Depth into the surface along -UpVector, and as far out along +UpVector) following a polyline — recessed panel lines and seams. */
	UFUNCTION(BlueprintCallable, Category = "Vibe3D|Modeling", meta = (AICallable, DisplayName = "Cut Groove Along Polyline"))
	static FModelingResult CutGrooveAlongPolyline(int32 Handle, const TArray<FVector>& Points, float Width = 1.f, float Depth = 1.f, FVector UpVector = FVector::UpVector);
};
