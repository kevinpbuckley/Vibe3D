// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Vibe3D/UModelingService.h"

#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshRemeshFunctions.h"
#include "GeometryScript/MeshSimplifyFunctions.h"
#include "GeometryScript/MeshSubdivideFunctions.h"
#include "GeometryScript/MeshDeformFunctions.h"
#include "GeometryScript/MeshVoxelFunctions.h"
#include "GeometryScript/MeshUVFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshRepairFunctions.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "GeometryScript/MeshTransformFunctions.h"
#include "GeometryScript/MeshDecompositionFunctions.h"
#include "GeometryScript/MeshPolygroupFunctions.h"
#include "GeometryScript/MeshMaterialFunctions.h"
#include "GeometryScript/MeshVertexColorFunctions.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshBakeFunctions.h"
#include "GeometryScript/MeshBoneWeightFunctions.h"
#include "GeometryScript/MeshPoolFunctions.h"
#include "GeometryScript/CollisionFunctions.h"
#include "GeometryScript/CreateNewAssetUtilityFunctions.h"
#include "GeometryScript/OpenSubdivUtilityFunctions.h"
#include "GeometryScript/MeshSpatialFunctions.h"
#include "GeometryScript/MeshSamplingFunctions.h"
#include "GeometryScript/ContainmentFunctions.h"
#include "GeometryScript/MeshComparisonFunctions.h"
#include "GeometryScript/MeshSelectionQueryFunctions.h"
#include "GeometryScript/SceneUtilityFunctions.h"
#include "Components/MeshComponent.h"

#include "Animation/Skeleton.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Misc/CoreDelegates.h"
#include "StaticMeshEditorSubsystem.h"
#include "StaticMeshEditorSubsystemHelpers.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Selections/MeshConnectedComponents.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "SkeletalMeshAttributes.h"
#include "AssetImportTask.h"
#include "AssetCompilingManager.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/ObjectRedirector.h"
#include "ScopedTransaction.h"
#include "EngineUtils.h"
#include "Components/SkinnedMeshComponent.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "MeshQueries.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogVibe3DModeling, Log, All);

using namespace UE::Geometry;

// =========================================================================
// Session: integer handles -> UDynamicMesh (kept alive for the editor session)
// =========================================================================

namespace
{
	/** A named selection plus the mesh counts it was made against, so a stale one can be refused. */
	struct FStoredSelection
	{
		FGeometryScriptMeshSelection Selection;
		int32 TriangleCount = 0;
		int32 VertexCount = 0;
	};

	/** Where a handle was read from, so a skeletal LOD can be written back in place and checked against what was loaded. */
	struct FSkeletalOrigin
	{
		FString AssetPath;
		int32 LODIndex = 0;
		int32 VertexCount = 0;
		int32 TriangleCount = 0;
		bool bFromRenderData = false;
	};

	/** A posed read of a skinned component: per-bone skinning matrices (reference pose -> current pose, in the space the handle is in). */
	struct FPoseBinding
	{
		FString Source;
		TArray<FMatrix> BoneSkinning;   // indexed like the handle's bone attribute
	};

	struct FModelingSession
	{
		TMap<int32, TStrongObjectPtr<UDynamicMesh>> Meshes;
		TMap<int32, TMap<FString, FStoredSelection>> Selections;
		TMap<int32, FSkeletalOrigin> Origins;
		TMap<int32, FPoseBinding> Poses;
		int32 NextHandle = 1;
		bool bExitHooked = false;

		void Reset()
		{
			Selections.Reset();
			Origins.Reset();
			Poses.Reset();
			Meshes.Reset();
		}
	};

	FModelingSession& Session()
	{
		static FModelingSession S;
		if (!S.bExitHooked)
		{
			// Release the strong references before the UObject system tears down.
			S.bExitHooked = true;
			FCoreDelegates::OnPreExit.AddLambda([]() { Session().Reset(); });
		}
		return S;
	}

	UDynamicMesh* FindMesh(int32 Handle)
	{
		const TStrongObjectPtr<UDynamicMesh>* Found = Session().Meshes.Find(Handle);
		return Found ? Found->Get() : nullptr;
	}

	int32 AddMesh(UDynamicMesh* Mesh)
	{
		const int32 Handle = Session().NextHandle++;
		Session().Meshes.Add(Handle, TStrongObjectPtr<UDynamicMesh>(Mesh));
		return Handle;
	}

	UDynamicMesh* NewSessionMesh(int32& OutHandle)
	{
		UDynamicMesh* Mesh = NewObject<UDynamicMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		OutHandle = AddMesh(Mesh);
		return Mesh;
	}

	UDynamicMesh* NewScratchMesh()
	{
		return NewObject<UDynamicMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	int32 TriCount(UDynamicMesh* Mesh)
	{
		return Mesh ? Mesh->GetTriangleCount() : 0;
	}

	int32 VertCount(UDynamicMesh* Mesh)
	{
		int32 Count = 0;
		if (Mesh)
		{
			Mesh->ProcessMesh([&Count](const FDynamicMesh3& M) { Count = M.VertexCount(); });
		}
		return Count;
	}

	// ---- results -----------------------------------------------------------

	FModelingResult Fail(int32 Handle, const FString& Message)
	{
		FModelingResult R;
		R.bSuccess = false;
		R.Handle = Handle;
		R.Message = Message;
		UE_LOG(LogVibe3DModeling, Warning, TEXT("ModelingService failure (handle %d): %s"), Handle, *Message);
		return R;
	}

	FModelingResult Ok(int32 Handle, UDynamicMesh* Mesh, const FString& Message)
	{
		FModelingResult R;
		R.bSuccess = true;
		R.Handle = Handle;
		R.Message = Message;
		R.TriangleCount = TriCount(Mesh);
		R.VertexCount = VertCount(Mesh);
		return R;
	}

	FModelingResult NoMesh(int32 Handle)
	{
		return Fail(Handle, FString::Printf(TEXT("No session mesh with handle %d (create_mesh / load_mesh_* first, or see list_meshes)"), Handle));
	}

	// ---- GeometryScript debug capture --------------------------------------

	UGeometryScriptDebug* NewDebug()
	{
		return NewObject<UGeometryScriptDebug>(GetTransientPackage(), NAME_None, RF_Transient);
	}

	bool DebugHasErrors(const UGeometryScriptDebug* Debug)
	{
		if (!Debug) { return false; }
		for (const FGeometryScriptDebugMessage& M : Debug->Messages)
		{
			if (M.MessageType == EGeometryScriptDebugMessageType::ErrorMessage || M.Message.ToString().Contains(TEXT("cannot be run"))) { return true; }
		}
		return false;
	}

	FString DebugText(const UGeometryScriptDebug* Debug)
	{
		TArray<FString> Lines;
		if (Debug)
		{
			for (const FGeometryScriptDebugMessage& M : Debug->Messages)
			{
				Lines.Add(M.Message.ToString());
			}
		}
		return FString::Join(Lines, TEXT("; "));
	}

	/** Standard end of a mutating call: errors reported by GeometryScript become a failure. */
	FModelingResult Finish(int32 Handle, UDynamicMesh* Mesh, UGeometryScriptDebug* Debug, const FString& What)
	{
		if (DebugHasErrors(Debug))
		{
			return Fail(Handle, What + TEXT(": ") + DebugText(Debug));
		}
		const FString Extra = DebugText(Debug);
		return Ok(Handle, Mesh, Extra.IsEmpty() ? What : What + TEXT(" (") + Extra + TEXT(")"));
	}

	// ---- enums by name -------------------------------------------------------

	template <typename TEnum>
	FString EnumNames()
	{
		TArray<FString> Names;
		const UEnum* E = StaticEnum<TEnum>();
		for (int32 i = 0; i < E->NumEnums() - 1; ++i)
		{
			Names.Add(E->GetNameStringByIndex(i));
		}
		return FString::Join(Names, TEXT(", "));
	}

	template <typename TEnum>
	bool ParseEnum(const FString& Name, TEnum& Out)
	{
		const UEnum* E = StaticEnum<TEnum>();
		const int64 Value = E->GetValueByNameString(Name.TrimStartAndEnd());
		if (Value == INDEX_NONE)
		{
			return false;
		}
		Out = static_cast<TEnum>(Value);
		return true;
	}

	// ---- selections ----------------------------------------------------------

	void StoreSelection(int32 Handle, const FString& Name, const FGeometryScriptMeshSelection& Selection)
	{
		FStoredSelection Stored;
		Stored.Selection = Selection;
		Stored.TriangleCount = TriCount(FindMesh(Handle));
		Stored.VertexCount = VertCount(FindMesh(Handle));
		Session().Selections.FindOrAdd(Handle).Add(Name, Stored);
	}

	/** Every element id the selection references must still exist — GeometryScript asserts on stale ids instead of failing. */
	bool SelectionIsValidForMesh(UDynamicMesh* Mesh, const FGeometryScriptMeshSelection& Selection)
	{
		FGeometryScriptIndexList IndexList;
		EGeometryScriptIndexType ResultType = EGeometryScriptIndexType::Any;
		UGeometryScriptLibrary_MeshSelectionFunctions::ConvertMeshSelectionToIndexList(Mesh, Selection, IndexList, ResultType, EGeometryScriptIndexType::Any);
		if (!IndexList.List.IsValid() || ResultType == EGeometryScriptIndexType::PolygroupID || ResultType == EGeometryScriptIndexType::Any)
		{
			return true;
		}
		bool bValid = true;
		Mesh->ProcessMesh([&](const FDynamicMesh3& M)
		{
			for (const int32 Id : *IndexList.List)
			{
				const bool bExists =
					ResultType == EGeometryScriptIndexType::Triangle ? M.IsTriangle(Id) :
					ResultType == EGeometryScriptIndexType::Vertex ? M.IsVertex(Id) :
					ResultType == EGeometryScriptIndexType::Edge ? M.IsEdge(Id) : true;
				if (!bExists) { bValid = false; return; }
			}
		});
		return bValid;
	}

	/** "" resolves to the whole mesh as a triangle selection; a named selection must predate no topology change. */
	bool ResolveSelection(int32 Handle, const FString& Name, UDynamicMesh* Mesh, FGeometryScriptMeshSelection& Out, FString& Error)
	{
		if (Name.IsEmpty())
		{
			UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(Mesh, Out, EGeometryScriptMeshSelectionType::Triangles);
			return true;
		}
		const TMap<FString, FStoredSelection>* PerMesh = Session().Selections.Find(Handle);
		const FStoredSelection* Found = PerMesh ? PerMesh->Find(Name) : nullptr;
		if (!Found)
		{
			Error = FString::Printf(TEXT("No selection named '%s' on handle %d (make one with select_all / select_by_normal_angle / select_in_box / ...)"), *Name, Handle);
			return false;
		}
		const int32 Tris = TriCount(Mesh);
		const int32 Verts = VertCount(Mesh);
		if (Tris != Found->TriangleCount || Verts != Found->VertexCount || !SelectionIsValidForMesh(Mesh, Found->Selection))
		{
			Error = FString::Printf(TEXT("Selection '%s' is stale: handle %d changed since it was made (%d -> %d triangles). Re-run the select_* call after any op that changes topology."),
				*Name, Handle, Found->TriangleCount, Tris);
			return false;
		}
		Out = Found->Selection;
		return true;
	}

	/** Extrude, offset and inset keep the edited region's triangle ids, so its named selection follows those faces into the next op. */
	FModelingResult KeepRegionSelection(const FModelingResult& Result, int32 Handle, const FString& Name, UDynamicMesh* Mesh, const FGeometryScriptMeshSelection& Selection)
	{
		if (Result.bSuccess && !Name.IsEmpty() && Selection.GetSelectionType() == EGeometryScriptMeshSelectionType::Triangles && SelectionIsValidForMesh(Mesh, Selection))
		{
			StoreSelection(Handle, Name, Selection);
		}
		return Result;
	}

	FGeometryScriptPrimitiveOptions PrimitiveOptions(int32 MaterialID)
	{
		FGeometryScriptPrimitiveOptions Options;
		Options.MaterialID = MaterialID;
		return Options;
	}

	/** Triangle-id watermark taken before an append; pass to StampMaterialIDOnNewTriangles. */
	int32 TriangleIDWatermark(UDynamicMesh* Mesh)
	{
		int32 MaxTid = 0;
		if (Mesh)
		{
			Mesh->ProcessMesh([&MaxTid](const FDynamicMesh3& M) { MaxTid = M.MaxTriangleID(); });
		}
		return MaxTid;
	}

	/**
	 * Some engine primitive generators — AppendTorus, AppendRevolvePolygon and AppendLinearStairs —
	 * ignore FGeometryScriptPrimitiveOptions::MaterialID, so a caller's material_id silently landed
	 * as 0 and the part could not be given its own material slot. Stamp the id onto the triangles
	 * the append just created (everything at or above the pre-call watermark).
	 */
	void StampMaterialIDOnNewTriangles(UDynamicMesh* Mesh, int32 FirstNewTriangleID, int32 MaterialID)
	{
		if (!Mesh || MaterialID == 0)
		{
			return;   // 0 is what the generators already produce
		}
		Mesh->EditMesh([FirstNewTriangleID, MaterialID](FDynamicMesh3& M)
		{
			if (!M.HasAttributes())
			{
				M.EnableAttributes();
			}
			if (!M.Attributes()->HasMaterialID())
			{
				M.Attributes()->EnableMaterialID();
			}
			FDynamicMeshMaterialAttribute* Materials = M.Attributes()->GetMaterialID();
			for (const int32 Tid : M.TriangleIndicesItr())
			{
				if (Tid >= FirstNewTriangleID)
				{
					Materials->SetValue(Tid, MaterialID);
				}
			}
		}, EDynamicMeshChangeType::AttributeEdit);   // no MaterialIDs flag exists; Unknown is the default hint
	}

	bool ParseOrigin(const FString& Origin, EGeometryScriptPrimitiveOriginMode& Out, FString& Error)
	{
		if (ParseEnum(Origin, Out)) { return true; }
		Error = FString::Printf(TEXT("Unknown origin '%s' (use %s)"), *Origin, *EnumNames<EGeometryScriptPrimitiveOriginMode>());
		return false;
	}

	// ---- assets ----------------------------------------------------------------

	template <typename T>
	T* LoadAssetAs(const FString& AssetPath)
	{
		// DoesAssetExist first: LoadAsset logs an error for a missing path, which agents (and automation) read as a failure of their own.
		if (AssetPath.IsEmpty() || !UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			return nullptr;
		}
		return Cast<T>(UEditorAssetLibrary::LoadAsset(AssetPath));
	}

	bool CopyFromStaticMesh(UStaticMesh* StaticMesh, UDynamicMesh* Target, int32 LODIndex, UGeometryScriptDebug* Debug)
	{
		FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
		FGeometryScriptMeshReadLOD ReadLOD;
		ReadLOD.LODIndex = LODIndex;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(StaticMesh, Target, AssetOptions, ReadLOD, Outcome, Debug);
		return Outcome == EGeometryScriptOutcomePins::Success;
	}

	bool CopyFromSkeletalMesh(USkeletalMesh* SkeletalMesh, UDynamicMesh* Target, int32 LODIndex, UGeometryScriptDebug* Debug)
	{
		FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
		FGeometryScriptMeshReadLOD ReadLOD;
		ReadLOD.LODIndex = LODIndex;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromSkeletalMesh(SkeletalMesh, Target, AssetOptions, ReadLOD, Outcome, Debug);
		return Outcome == EGeometryScriptOutcomePins::Success;
	}

	void SaveIf(bool bSave, const FString& AssetPath)
	{
		if (bSave)
		{
			UEditorAssetLibrary::SaveAsset(AssetPath, false);
		}
	}

	AActor* FindActorByLabel(const FString& Label)
	{
		UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		if (!ActorSubsystem) { return nullptr; }
		for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
		{
			if (Actor && Actor->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			{
				return Actor;
			}
		}
		return nullptr;
	}
}

// =========================================================================
// Session
// =========================================================================

FModelingResult UModelingService::CreateMesh()
{
	int32 Handle = -1;
	UDynamicMesh* Mesh = NewSessionMesh(Handle);
	return Ok(Handle, Mesh, TEXT("Created empty mesh"));
}

FModelingResult UModelingService::LoadMeshFromStaticMesh(const FString& AssetPath, int32 LODIndex)
{
	UStaticMesh* StaticMesh = LoadAssetAs<UStaticMesh>(AssetPath);
	if (!StaticMesh)
	{
		return Fail(-1, FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath));
	}
	int32 Handle = -1;
	UDynamicMesh* Mesh = NewSessionMesh(Handle);
	UGeometryScriptDebug* Debug = NewDebug();
	if (!CopyFromStaticMesh(StaticMesh, Mesh, LODIndex, Debug))
	{
		ReleaseMesh(Handle);
		return Fail(-1, FString::Printf(TEXT("Could not read LOD %d of %s: %s"), LODIndex, *AssetPath, *DebugText(Debug)));
	}
	FModelingResult R = Ok(Handle, Mesh, FString::Printf(TEXT("Loaded %s LOD %d"), *AssetPath, LODIndex));
	R.AssetPath = AssetPath;
	return R;
}

FModelingResult UModelingService::LoadMeshFromSkeletalMesh(const FString& AssetPath, int32 LODIndex)
{
	USkeletalMesh* SkeletalMesh = LoadAssetAs<USkeletalMesh>(AssetPath);
	if (!SkeletalMesh)
	{
		return Fail(-1, FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath));
	}
	if (LODIndex < 0 || LODIndex >= SkeletalMesh->GetLODNum())
	{
		return Fail(-1, FString::Printf(TEXT("%s has %d LOD(s); LOD %d does not exist (GeometryScript would silently read another one)"), *AssetPath, SkeletalMesh->GetLODNum(), LODIndex));
	}
	// Source model when the LOD has one (the editable mesh, the vertex layout a write-back expects); a LOD generated by
	// reduction only has render data, whose vertices are split along UV and normal seams.
	const bool bFromRenderData = !SkeletalMesh->HasMeshDescription(LODIndex);
	int32 Handle = -1;
	UDynamicMesh* Mesh = NewSessionMesh(Handle);
	UGeometryScriptDebug* Debug = NewDebug();
	if (!CopyFromSkeletalMesh(SkeletalMesh, Mesh, LODIndex, Debug))
	{
		ReleaseMesh(Handle);
		return Fail(-1, FString::Printf(TEXT("Could not read LOD %d of %s: %s"), LODIndex, *AssetPath, *DebugText(Debug)));
	}
	FSkeletalOrigin Origin;
	Origin.AssetPath = AssetPath;
	Origin.LODIndex = LODIndex;
	Origin.VertexCount = VertCount(Mesh);
	Origin.TriangleCount = TriCount(Mesh);
	Origin.bFromRenderData = bFromRenderData;
	Session().Origins.Add(Handle, Origin);
	FModelingResult R = Ok(Handle, Mesh, FString::Printf(TEXT("Loaded %s LOD %d of %d (with bone weights, %s)%s"), *AssetPath, LODIndex, SkeletalMesh->GetLODNum(),
		bFromRenderData ? TEXT("render data: this LOD is generated by reduction") : TEXT("source model"),
		bFromRenderData ? TEXT("; writing it back turns it into a custom LOD") : TEXT("; save_mesh_to_skeletal_mesh_lod(handle) writes it back in place")));
	R.AssetPath = AssetPath;
	return R;
}

FModelingResult UModelingService::LoadMeshFromActor(const FString& ActorLabel, bool bWorldSpace, int32 LODIndex)
{
	AActor* Actor = FindActorByLabel(ActorLabel);
	if (!Actor)
	{
		return Fail(-1, FString::Printf(TEXT("No level actor labeled '%s'"), *ActorLabel));
	}
	UStaticMeshComponent* StaticComponent = Actor->FindComponentByClass<UStaticMeshComponent>();
	UStaticMesh* StaticMesh = StaticComponent ? StaticComponent->GetStaticMesh() : nullptr;
	int32 Handle = -1;
	UDynamicMesh* Mesh = NewSessionMesh(Handle);
	UGeometryScriptDebug* Debug = NewDebug();
	if (StaticMesh)
	{
		if (!CopyFromStaticMesh(StaticMesh, Mesh, LODIndex, Debug))
		{
			ReleaseMesh(Handle);
			return Fail(-1, FString::Printf(TEXT("Could not read %s: %s"), *StaticMesh->GetPathName(), *DebugText(Debug)));
		}
		if (bWorldSpace)
		{
			UGeometryScriptLibrary_MeshTransformFunctions::TransformMesh(Mesh, StaticComponent->GetComponentTransform(), true, Debug);
		}
		FModelingResult R = Ok(Handle, Mesh, FString::Printf(TEXT("Loaded %s from actor '%s'%s"), *StaticMesh->GetPathName(), *ActorLabel,
			bWorldSpace ? TEXT(" in world space") : TEXT(" in asset space")));
		R.AssetPath = StaticMesh->GetPathName();
		return R;
	}

	// Anything else that renders a mesh (skeletal mesh, dynamic mesh, instanced components): copy its render geometry.
	UMeshComponent* MeshComponent = Actor->FindComponentByClass<UMeshComponent>();
	if (!MeshComponent)
	{
		ReleaseMesh(Handle);
		return Fail(-1, FString::Printf(TEXT("Actor '%s' has no mesh component"), *ActorLabel));
	}
	FGeometryScriptCopyMeshFromComponentOptions Options;
	Options.RequestedLOD.LODIndex = LODIndex;
	FTransform LocalToWorld;
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_SceneUtilityFunctions::CopyMeshFromComponent(MeshComponent, Mesh, Options, bWorldSpace, LocalToWorld, Outcome, Debug);
	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		ReleaseMesh(Handle);
		return Fail(-1, FString::Printf(TEXT("Could not copy geometry from '%s' (%s): %s"), *ActorLabel, *MeshComponent->GetClass()->GetName(), *DebugText(Debug)));
	}
	return Ok(Handle, Mesh, FString::Printf(TEXT("Loaded %s geometry from actor '%s'%s"), *MeshComponent->GetClass()->GetName(), *ActorLabel,
		bWorldSpace ? TEXT(" in world space") : TEXT(" in component space")));
}

FModelingResult UModelingService::CopyMesh(int32 Handle)
{
	UDynamicMesh* Source = FindMesh(Handle);
	if (!Source) { return NoMesh(Handle); }
	int32 NewHandle = -1;
	UDynamicMesh* Target = NewSessionMesh(NewHandle);
	UDynamicMesh* Out = nullptr;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDecompositionFunctions::CopyMeshToMesh(Source, Target, Out, Debug);
	// A copy is the same mesh: it keeps where it was loaded from, and a posed copy stays posed (so it cannot be saved as a reference pose by mistake).
	if (const FSkeletalOrigin* Origin = Session().Origins.Find(Handle)) { const FSkeletalOrigin Copy = *Origin; Session().Origins.Add(NewHandle, Copy); }
	if (const FPoseBinding* Pose = Session().Poses.Find(Handle)) { const FPoseBinding Copy = *Pose; Session().Poses.Add(NewHandle, Copy); }
	return Finish(NewHandle, Target, Debug, FString::Printf(TEXT("Copied handle %d"), Handle));
}

bool UModelingService::ReleaseMesh(int32 Handle)
{
	Session().Selections.Remove(Handle);
	Session().Origins.Remove(Handle);
	Session().Poses.Remove(Handle);
	return Session().Meshes.Remove(Handle) > 0;
}

int32 UModelingService::ReleaseAllMeshes()
{
	const int32 Count = Session().Meshes.Num();
	Session().Reset();
	return Count;
}

TArray<int32> UModelingService::ListMeshes()
{
	TArray<int32> Handles;
	Session().Meshes.GetKeys(Handles);
	Handles.Sort();
	return Handles;
}

FModelingMeshInfo UModelingService::GetMeshInfo(int32 Handle)
{
	FModelingMeshInfo Info;
	Info.Handle = Handle;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		Info.Message = NoMesh(Handle).Message;
		return Info;
	}
	Info.bSuccess = true;
	Info.TriangleCount = TriCount(Mesh);
	Info.bIsClosed = UGeometryScriptLibrary_MeshQueryFunctions::GetIsClosedMesh(Mesh);
	Info.OpenBorderEdges = UGeometryScriptLibrary_MeshQueryFunctions::GetNumOpenBorderEdges(Mesh);
	Info.ConnectedComponents = UGeometryScriptLibrary_MeshQueryFunctions::GetNumConnectedComponents(Mesh);
	const FBox Bounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(Mesh);
	Info.BoundsMin = Bounds.Min;
	Info.BoundsMax = Bounds.Max;
	UGeometryScriptLibrary_MeshQueryFunctions::GetMeshVolumeArea(Mesh, Info.SurfaceArea, Info.Volume);
	Mesh->ProcessMesh([&Info](const FDynamicMesh3& M)
	{
		Info.VertexCount = M.VertexCount();
		if (M.HasAttributes())
		{
			Info.NumUVLayers = M.Attributes()->NumUVLayers();
			Info.bHasVertexColors = M.Attributes()->HasPrimaryColors();
			if (M.Attributes()->HasMaterialID())
			{
				TSet<int32> Ids;
				const FDynamicMeshMaterialAttribute* Materials = M.Attributes()->GetMaterialID();
				for (const int32 Tid : M.TriangleIndicesItr())
				{
					Ids.Add(Materials->GetValue(Tid));
				}
				Info.MaterialIds = Ids.Array();
				Info.MaterialIds.Sort();
			}
		}
	});
	if (const TMap<FString, FStoredSelection>* PerMesh = Session().Selections.Find(Handle))
	{
		PerMesh->GetKeys(Info.Selections);
	}
	Info.Message = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshInfoString(Mesh);
	return Info;
}

UDynamicMesh* UModelingService::GetDynamicMesh(int32 Handle)
{
	return FindMesh(Handle);
}

TArray<FVector> UModelingService::GetVertexPositions(int32 Handle)
{
	TArray<FVector> Out;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		UE_LOG(LogVibe3DModeling, Warning, TEXT("GetVertexPositions: %s"), *NoMesh(Handle).Message);
		return Out;
	}
	Mesh->ProcessMesh([&Out](const FDynamicMesh3& M)
	{
		Out.SetNumZeroed(M.MaxVertexID());
		for (const int32 Vid : M.VertexIndicesItr())
		{
			Out[Vid] = M.GetVertex(Vid);
		}
	});
	return Out;
}

FModelingResult UModelingService::SetVertexPositions(int32 Handle, const TArray<FVector>& Positions)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	int32 MaxVid = 0;
	int32 NumVerts = 0;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M) { MaxVid = M.MaxVertexID(); NumVerts = M.VertexCount(); });
	if (Positions.Num() != MaxVid)
	{
		return Fail(Handle, FString::Printf(TEXT("Positions has %d entries but handle %d has %d vertex IDs (%d vertices%s): pass the list get_vertex_positions returned, edited in place"),
			Positions.Num(), Handle, MaxVid, NumVerts, MaxVid != NumVerts ? TEXT(", with ID gaps") : TEXT("")));
	}
	int32 Moved = 0;
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		for (const int32 Vid : M.VertexIndicesItr())
		{
			if (!M.GetVertex(Vid).Equals(Positions[Vid], 0.0))
			{
				M.SetVertex(Vid, Positions[Vid]);
				++Moved;
			}
		}
	}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions);
	return Ok(Handle, Mesh, FString::Printf(TEXT("Set %d vertex positions (%d moved)"), NumVerts, Moved));
}

// =========================================================================
// Primitives
// =========================================================================

FModelingResult UModelingService::AppendBox(int32 Handle, FTransform Transform, float DimensionX, float DimensionY, float DimensionZ,
	int32 StepsX, int32 StepsY, int32 StepsZ, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Mesh, PrimitiveOptions(MaterialID), Transform, DimensionX, DimensionY, DimensionZ,
		StepsX, StepsY, StepsZ, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended box"));
}

FModelingResult UModelingService::AppendSphere(int32 Handle, FTransform Transform, float Radius, int32 StepsPhi, int32 StepsTheta,
	const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(Mesh, PrimitiveOptions(MaterialID), Transform, Radius, StepsPhi, StepsTheta, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended sphere"));
}

FModelingResult UModelingService::AppendCylinder(int32 Handle, FTransform Transform, float Radius, float Height, int32 RadialSteps,
	int32 HeightSteps, bool bCapped, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(Mesh, PrimitiveOptions(MaterialID), Transform, Radius, Height, RadialSteps, HeightSteps,
		bCapped, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended cylinder"));
}

FModelingResult UModelingService::AppendCone(int32 Handle, FTransform Transform, float BaseRadius, float TopRadius, float Height,
	int32 RadialSteps, int32 HeightSteps, bool bCapped, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(Mesh, PrimitiveOptions(MaterialID), Transform, BaseRadius, TopRadius, Height, RadialSteps,
		HeightSteps, bCapped, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended cone"));
}

FModelingResult UModelingService::AppendCapsule(int32 Handle, FTransform Transform, float Radius, float LineLength, int32 HemisphereSteps,
	int32 CircleSteps, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCapsule(Mesh, PrimitiveOptions(MaterialID), Transform, Radius, LineLength, HemisphereSteps,
		CircleSteps, 0, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended capsule"));
}

FModelingResult UModelingService::AppendTorus(int32 Handle, FTransform Transform, float MajorRadius, float MinorRadius, int32 MajorSteps,
	int32 MinorSteps, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptRevolveOptions RevolveOptions;
	const int32 Watermark = TriangleIDWatermark(Mesh);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(Mesh, PrimitiveOptions(MaterialID), Transform, RevolveOptions, MajorRadius, MinorRadius,
		MajorSteps, MinorSteps, OriginMode, Debug);
	StampMaterialIDOnNewTriangles(Mesh, Watermark, MaterialID);   // the generator drops PrimitiveOptions.MaterialID
	return Finish(Handle, Mesh, Debug, TEXT("Appended torus"));
}

FModelingResult UModelingService::AppendRectangle(int32 Handle, FTransform Transform, float DimensionX, float DimensionY, int32 StepsWidth,
	int32 StepsHeight, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangleXY(Mesh, PrimitiveOptions(MaterialID), Transform, DimensionX, DimensionY, StepsWidth, StepsHeight, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended rectangle"));
}

FModelingResult UModelingService::AppendDisc(int32 Handle, FTransform Transform, float Radius, int32 AngleSteps, int32 SpokeSteps,
	float StartAngle, float EndAngle, float HoleRadius, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendDisc(Mesh, PrimitiveOptions(MaterialID), Transform, Radius, AngleSteps, SpokeSteps, StartAngle, EndAngle, HoleRadius, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended disc"));
}

FModelingResult UModelingService::AppendStairs(int32 Handle, FTransform Transform, float StepWidth, float StepHeight, float StepDepth,
	int32 NumSteps, bool bFloating, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	const int32 Watermark = TriangleIDWatermark(Mesh);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendLinearStairs(Mesh, PrimitiveOptions(MaterialID), Transform, StepWidth, StepHeight, StepDepth, NumSteps, bFloating, Debug);
	StampMaterialIDOnNewTriangles(Mesh, Watermark, MaterialID);   // the generator drops PrimitiveOptions.MaterialID
	return Finish(Handle, Mesh, Debug, TEXT("Appended stairs"));
}

FModelingResult UModelingService::AppendExtrudePolygon(int32 Handle, FTransform Transform, const TArray<FVector2D>& PolygonPoints, float Height,
	int32 HeightSteps, bool bCapped, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (PolygonPoints.Num() < 3) { return Fail(Handle, TEXT("PolygonPoints needs at least 3 points")); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(Mesh, PrimitiveOptions(MaterialID), Transform, PolygonPoints, Height, HeightSteps,
		bCapped, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended extruded polygon"));
}

FModelingResult UModelingService::AppendRevolvePolygon(int32 Handle, FTransform Transform, const TArray<FVector2D>& ProfilePoints, float Radius,
	int32 Steps, float RevolveDegrees, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (ProfilePoints.Num() < 3) { return Fail(Handle, TEXT("ProfilePoints needs at least 3 points")); }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptRevolveOptions RevolveOptions;
	RevolveOptions.RevolveDegrees = RevolveDegrees;
	const int32 Watermark = TriangleIDWatermark(Mesh);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePolygon(Mesh, PrimitiveOptions(MaterialID), Transform, ProfilePoints, RevolveOptions, Radius, Steps, Debug);
	StampMaterialIDOnNewTriangles(Mesh, Watermark, MaterialID);   // the generator drops PrimitiveOptions.MaterialID
	return Finish(Handle, Mesh, Debug, TEXT("Appended revolved profile"));
}

FModelingResult UModelingService::AppendMesh(int32 Handle, int32 OtherHandle, FTransform Transform)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UDynamicMesh* Other = FindMesh(OtherHandle);
	if (!Other) { return NoMesh(OtherHandle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Other, Transform, false, FGeometryScriptAppendMeshOptions(), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Appended handle %d"), OtherHandle));
}

// =========================================================================
// Booleans
// =========================================================================

FModelingResult UModelingService::Boolean(int32 TargetHandle, int32 ToolHandle, const FString& Operation, FTransform ToolTransform,
	bool bFillHoles, bool bSimplifyOutput)
{
	UDynamicMesh* Target = FindMesh(TargetHandle);
	if (!Target) { return NoMesh(TargetHandle); }
	UDynamicMesh* Tool = FindMesh(ToolHandle);
	if (!Tool) { return NoMesh(ToolHandle); }
	EGeometryScriptBooleanOperation Op;
	if (!ParseEnum(Operation, Op))
	{
		return Fail(TargetHandle, FString::Printf(TEXT("Unknown boolean operation '%s' (use %s)"), *Operation, *EnumNames<EGeometryScriptBooleanOperation>()));
	}
	FGeometryScriptMeshBooleanOptions Options;
	Options.bFillHoles = bFillHoles;
	Options.bSimplifyOutput = bSimplifyOutput;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Target, FTransform::Identity, Tool, ToolTransform, Op, Options, Debug);
	return Finish(TargetHandle, Target, Debug, FString::Printf(TEXT("Boolean %s with handle %d"), *Operation, ToolHandle));
}

FModelingResult UModelingService::SelfUnion(int32 Handle, bool bFillHoles, bool bTrimFlaps)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelfUnionOptions Options;
	Options.bFillHoles = bFillHoles;
	Options.bTrimFlaps = bTrimFlaps;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Self union"));
}

FModelingResult UModelingService::PlaneCut(int32 Handle, FTransform CutFrame, bool bFillHoles, bool bFlipCutSide)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshPlaneCutOptions Options;
	Options.bFillHoles = bFillHoles;
	Options.bFlipCutSide = bFlipCutSide;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshPlaneCut(Mesh, CutFrame, Options, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Plane cut"));
}

FModelingResult UModelingService::Mirror(int32 Handle, FTransform MirrorFrame, bool bApplyPlaneCut, bool bWeldAlongPlane, bool bFlipCutSide)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshMirrorOptions Options;
	Options.bApplyPlaneCut = bApplyPlaneCut;
	Options.bWeldAlongPlane = bWeldAlongPlane;
	Options.bFlipCutSide = bFlipCutSide;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshMirror(Mesh, MirrorFrame, Options, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Mirror"));
}

// =========================================================================
// Selections
// =========================================================================

int32 UModelingService::SelectAll(int32 Handle, const FString& SelectionName, const FString& SelectionType)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	EGeometryScriptMeshSelectionType Type = EGeometryScriptMeshSelectionType::Triangles;
	if (!SelectionType.IsEmpty() && !ParseEnum(SelectionType, Type))
	{
		UE_LOG(LogVibe3DModeling, Warning, TEXT("Unknown selection type '%s' (use %s)"), *SelectionType, *EnumNames<EGeometryScriptMeshSelectionType>());
		return -1;
	}
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(Mesh, Selection, Type);
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

int32 UModelingService::SelectByNormalAngle(int32 Handle, const FString& SelectionName, FVector Normal, float MaxAngleDeg, bool bInvert)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::SelectMeshElementsByNormalAngle(Mesh, Selection, Normal.GetSafeNormal(), MaxAngleDeg,
		EGeometryScriptMeshSelectionType::Triangles, bInvert, 3);
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

int32 UModelingService::SelectInBox(int32 Handle, const FString& SelectionName, FVector BoxMin, FVector BoxMax, bool bInvert)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::SelectMeshElementsInBox(Mesh, Selection, FBox(BoxMin, BoxMax), EGeometryScriptMeshSelectionType::Triangles, bInvert, 3);
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

int32 UModelingService::SelectInSphere(int32 Handle, const FString& SelectionName, FVector Center, float Radius, bool bInvert)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::SelectMeshElementsInSphere(Mesh, Selection, Center, Radius, EGeometryScriptMeshSelectionType::Triangles, bInvert, 3);
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

int32 UModelingService::SelectByMaterialID(int32 Handle, const FString& SelectionName, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	TArray<int32> TriangleIDs;
	Mesh->ProcessMesh([&TriangleIDs, MaterialID](const FDynamicMesh3& M)
	{
		if (M.HasAttributes() && M.Attributes()->HasMaterialID())
		{
			const FDynamicMeshMaterialAttribute* Materials = M.Attributes()->GetMaterialID();
			for (const int32 Tid : M.TriangleIndicesItr())
			{
				if (Materials->GetValue(Tid) == MaterialID)
				{
					TriangleIDs.Add(Tid);
				}
			}
		}
	});
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(Mesh, TriangleIDs, EGeometryScriptMeshSelectionType::Triangles, Selection);
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

int32 UModelingService::ExpandContractSelection(int32 Handle, const FString& SelectionName, const FString& NewSelectionName, int32 Iterations, bool bContract)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || NewSelectionName.IsEmpty()) { return -1; }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { UE_LOG(LogVibe3DModeling, Warning, TEXT("%s"), *Error); return -1; }
	FGeometryScriptMeshSelection NewSelection;
	UGeometryScriptLibrary_MeshSelectionFunctions::ExpandContractMeshSelection(Mesh, Selection, NewSelection, Iterations, bContract, false);
	StoreSelection(Handle, NewSelectionName, NewSelection);
	return NewSelection.GetNumSelected();
}

int32 UModelingService::InvertSelection(int32 Handle, const FString& SelectionName, const FString& NewSelectionName)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || NewSelectionName.IsEmpty()) { return -1; }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { UE_LOG(LogVibe3DModeling, Warning, TEXT("%s"), *Error); return -1; }
	FGeometryScriptMeshSelection NewSelection;
	UGeometryScriptLibrary_MeshSelectionFunctions::InvertMeshSelection(Mesh, Selection, NewSelection, false);
	StoreSelection(Handle, NewSelectionName, NewSelection);
	return NewSelection.GetNumSelected();
}

int32 UModelingService::SelectionCount(int32 Handle, const FString& SelectionName)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return -1; }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return -1; }
	return Selection.GetNumSelected();
}

bool UModelingService::ClearSelections(int32 Handle)
{
	return Session().Selections.Remove(Handle) > 0;
}

// =========================================================================
// Poly-edit operations
// =========================================================================

FModelingResult UModelingService::ExtrudeFaces(int32 Handle, const FString& SelectionName, float Distance, FVector Direction)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptMeshLinearExtrudeOptions Options;
	Options.Distance = Distance;
	if (Direction.IsNearlyZero())
	{
		Options.DirectionMode = EGeometryScriptLinearExtrudeDirection::AverageFaceNormal;
	}
	else
	{
		Options.DirectionMode = EGeometryScriptLinearExtrudeDirection::FixedDirection;
		Options.Direction = Direction.GetSafeNormal();
	}
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshLinearExtrudeFaces(Mesh, Options, Selection, Debug);
	return KeepRegionSelection(Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Extruded %d faces by %.2f"), Selection.GetNumSelected(), Distance)), Handle, SelectionName, Mesh, Selection);
}

FModelingResult UModelingService::OffsetFaces(int32 Handle, const FString& SelectionName, float Distance)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptMeshOffsetFacesOptions Options;
	Options.Distance = Distance;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshOffsetFaces(Mesh, Options, Selection, Debug);
	return KeepRegionSelection(Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Offset %d faces by %.2f"), Selection.GetNumSelected(), Distance)), Handle, SelectionName, Mesh, Selection);
}

FModelingResult UModelingService::InsetFaces(int32 Handle, const FString& SelectionName, float Distance, float Softness)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptMeshInsetOutsetFacesOptions Options;
	Options.Distance = Distance;
	Options.Softness = Softness;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshInsetOutsetFaces(Mesh, Options, Selection, Debug);
	return KeepRegionSelection(Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Inset %d faces by %.2f"), Selection.GetNumSelected(), Distance)), Handle, SelectionName, Mesh, Selection);
}

FModelingResult UModelingService::OutsetFaces(int32 Handle, const FString& SelectionName, float Distance, float Softness)
{
	// GeometryScript's inset/outset op uses the sign of the distance: negative = outset.
	FModelingResult R = InsetFaces(Handle, SelectionName, -FMath::Abs(Distance), Softness);
	if (R.bSuccess) { R.Message = R.Message.Replace(TEXT("Inset"), TEXT("Outset")); }
	return R;
}

FModelingResult UModelingService::DeleteFaces(int32 Handle, const FString& SelectionName)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	int32 NumDeleted = 0;
	UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteSelectedTrianglesFromMesh(Mesh, Selection, NumDeleted, false);
	return Ok(Handle, Mesh, FString::Printf(TEXT("Deleted %d triangles"), NumDeleted));
}

FModelingResult UModelingService::TranslateSelection(int32 Handle, const FString& SelectionName, FVector Delta)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshTransformFunctions::TranslateMeshSelection(Mesh, Selection, Delta, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Translated selection"));
}

FModelingResult UModelingService::BevelPolygroups(int32 Handle, float Distance, int32 Subdivisions, float RoundWeight)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshBevelOptions Options;
	Options.BevelDistance = Distance;
	Options.Subdivisions = Subdivisions;
	Options.RoundWeight = RoundWeight;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshPolygroupBevel(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Beveled polygroup edges by %.2f"), Distance));
}

FModelingResult UModelingService::OffsetMesh(int32 Handle, float Distance)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshOffsetOptions Options;
	Options.OffsetDistance = Distance;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshOffset(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Offset surface by %.2f"), Distance));
}

FModelingResult UModelingService::ShellMesh(int32 Handle, float Thickness)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshOffsetOptions Options;
	Options.OffsetDistance = Thickness;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshShell(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Shelled with thickness %.2f"), Thickness));
}

// =========================================================================
// Mesh processing
// =========================================================================

FModelingResult UModelingService::Remesh(int32 Handle, int32 TargetTriangleCount, float EdgeLength, bool bReprojectToInput)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptRemeshOptions RemeshOptions;
	RemeshOptions.bReprojectToInputMesh = bReprojectToInput;
	FGeometryScriptUniformRemeshOptions UniformOptions;
	if (EdgeLength > 0.f)
	{
		UniformOptions.TargetType = EGeometryScriptUniformRemeshTargetType::TargetEdgeLength;
		UniformOptions.TargetEdgeLength = EdgeLength;
	}
	else
	{
		UniformOptions.TargetType = EGeometryScriptUniformRemeshTargetType::TriangleCount;
		UniformOptions.TargetTriangleCount = FMath::Max(4, TargetTriangleCount);
	}
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_RemeshingFunctions::ApplyUniformRemesh(Mesh, RemeshOptions, UniformOptions, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Remeshed"));
}

FModelingResult UModelingService::SimplifyToTriangleCount(int32 Handle, int32 TriangleCount, bool bAllowSeamCollapse)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptSimplifyMeshOptions Options;
	Options.bAllowSeamCollapse = bAllowSeamCollapse;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTriangleCount(Mesh, FMath::Max(4, TriangleCount), Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Simplified toward %d triangles"), TriangleCount));
}

FModelingResult UModelingService::SimplifyToTolerance(int32 Handle, float Tolerance, bool bAllowSeamCollapse)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptSimplifyMeshOptions Options;
	Options.bAllowSeamCollapse = bAllowSeamCollapse;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToTolerance(Mesh, Tolerance, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Simplified to tolerance %.3f"), Tolerance));
}

FModelingResult UModelingService::Subdivide(int32 Handle, int32 Level, const FString& Method)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	Level = FMath::Clamp(Level, 1, 6);
	UGeometryScriptDebug* Debug = NewDebug();
	const FString M = Method.TrimStartAndEnd();
	UDynamicMesh* Result = Mesh;
	if (M.Equals(TEXT("PN"), ESearchCase::IgnoreCase))
	{
		FGeometryScriptPNTessellateOptions Options;
		Result = UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyPNTessellation(Mesh, Options, Level, Debug);
	}
	else if (M.Equals(TEXT("Uniform"), ESearchCase::IgnoreCase))
	{
		Result = UGeometryScriptLibrary_MeshSubdivideFunctions::ApplyUniformTessellation(Mesh, Level, Debug);
	}
	else if (M.Equals(TEXT("CatmullClark"), ESearchCase::IgnoreCase))
	{
		Result = UGeometryScriptLibrary_OpenSubdivFunctions::ApplyPolygroupCatmullClarkSubD(Mesh, Level, FGeometryScriptGroupLayer(), Debug);
	}
	else if (M.Equals(TEXT("Loop"), ESearchCase::IgnoreCase))
	{
		Result = UGeometryScriptLibrary_OpenSubdivFunctions::ApplyTriangleLoopSubD(Mesh, Level, Debug);
	}
	else
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown subdivide method '%s' (use PN, Uniform, CatmullClark, Loop)"), *Method));
	}
	if (Result && Result != Mesh)
	{
		UDynamicMesh* Out = nullptr;
		UGeometryScriptLibrary_MeshDecompositionFunctions::CopyMeshToMesh(Result, Mesh, Out, Debug);
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Subdivided (%s, level %d)"), *M, Level));
}

FModelingResult UModelingService::Smooth(int32 Handle, const FString& SelectionName, int32 Iterations, float Alpha)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptIterativeMeshSmoothingOptions Options;
	Options.NumIterations = FMath::Max(1, Iterations);
	Options.Alpha = Alpha;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyIterativeSmoothingToMesh(Mesh, Selection, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Smoothed (%d iterations)"), Iterations));
}

FModelingResult UModelingService::FillHoles(int32 Handle)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptFillHolesOptions Options;
	int32 NumFilled = 0, NumFailed = 0;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshRepairFunctions::FillAllMeshHoles(Mesh, Options, NumFilled, NumFailed, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Filled %d holes (%d failed)"), NumFilled, NumFailed));
}

FModelingResult UModelingService::WeldEdges(int32 Handle, float Tolerance)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptWeldEdgesOptions Options;
	Options.Tolerance = Tolerance;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshRepairFunctions::WeldMeshEdges(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Welded edges"));
}

FModelingResult UModelingService::Repair(int32 Handle, float MinComponentVolume, int32 MinComponentTriangles, bool bKeepClosed)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	const int32 Before = TriCount(Mesh);
	const bool bWasClosed = UGeometryScriptLibrary_MeshQueryFunctions::GetIsClosedMesh(Mesh);
	FGeometryScriptDegenerateTriangleOptions DegenerateOptions;
	UGeometryScriptLibrary_MeshRepairFunctions::RepairMeshDegenerateGeometry(Mesh, DegenerateOptions, Debug);
	FGeometryScriptRemoveSmallComponentOptions SmallOptions;
	SmallOptions.MinVolume = MinComponentVolume;
	SmallOptions.MinTriangleCount = MinComponentTriangles;
	UGeometryScriptLibrary_MeshRepairFunctions::RemoveSmallComponents(Mesh, SmallOptions, Debug);

	// Collapsing degenerate triangles can punch holes in a mesh that was watertight, which then
	// silently breaks the next boolean / voxel op (they need closed input) and shows as cracks in
	// the saved asset. Put them back when repair is what opened the mesh.
	FString HoleNote;
	if (bKeepClosed && bWasClosed && !UGeometryScriptLibrary_MeshQueryFunctions::GetIsClosedMesh(Mesh))
	{
		FGeometryScriptFillHolesOptions FillOptions;
		int32 NumFilled = 0, NumFailed = 0;
		UGeometryScriptLibrary_MeshRepairFunctions::FillAllMeshHoles(Mesh, FillOptions, NumFilled, NumFailed, Debug);
		HoleNote = FString::Printf(TEXT(", re-closed %d hole(s)%s"), NumFilled,
			NumFailed > 0 ? *FString::Printf(TEXT(" (%d could not be filled)"), NumFailed) : TEXT(""));
	}
	UGeometryScriptLibrary_MeshRepairFunctions::CompactMesh(Mesh, Debug);
	return Finish(Handle, Mesh, Debug,
		FString::Printf(TEXT("Repaired (%d -> %d triangles%s)"), Before, TriCount(Mesh), *HoleNote));
}

FModelingResult UModelingService::RemoveHiddenTriangles(int32 Handle)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptRemoveHiddenTrianglesOptions Options;
	UGeometryScriptDebug* Debug = NewDebug();
	const int32 Before = TriCount(Mesh);
	UGeometryScriptLibrary_MeshRepairFunctions::RemoveHiddenTriangles(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Removed %d hidden triangles"), Before - TriCount(Mesh)));
}

FModelingSplitResult UModelingService::SplitByComponents(int32 Handle)
{
	FModelingSplitResult Result;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		Result.Message = NoMesh(Handle).Message;
		return Result;
	}
	UDynamicMeshPool* Pool = UGeometryScriptLibrary_MeshPoolFunctions::GetGlobalMeshPool();
	TArray<UDynamicMesh*> Parts;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDecompositionFunctions::SplitMeshByComponents(Mesh, Parts, Pool, Debug);
	if (DebugHasErrors(Debug))
	{
		Result.Message = DebugText(Debug);
		return Result;
	}
	TArray<TPair<int32, int32>> HandlesByTriangles; // (triangle count, handle)
	for (UDynamicMesh* Part : Parts)
	{
		if (!Part) { continue; }
		int32 NewHandle = -1;
		UDynamicMesh* Target = NewSessionMesh(NewHandle);
		UDynamicMesh* Out = nullptr;
		UGeometryScriptLibrary_MeshDecompositionFunctions::CopyMeshToMesh(Part, Target, Out, Debug);
		HandlesByTriangles.Emplace(TriCount(Target), NewHandle);
		if (Pool) { Pool->ReturnMesh(Part); }
	}
	HandlesByTriangles.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Key > B.Key; });
	for (const TPair<int32, int32>& Entry : HandlesByTriangles)
	{
		Result.Handles.Add(Entry.Value);
	}
	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("Split into %d components (largest first); source handle %d unchanged"), Result.Handles.Num(), Handle);
	return Result;
}

// =========================================================================
// Deformation
// =========================================================================

FModelingResult UModelingService::Bend(int32 Handle, FTransform Orientation, float AngleDeg, float Extent, bool bBidirectional)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptBendWarpOptions Options;
	Options.bBidirectional = bBidirectional;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyBendWarpToMesh(Mesh, Options, Orientation, AngleDeg, Extent, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Bent %.1f degrees"), AngleDeg));
}

FModelingResult UModelingService::Twist(int32 Handle, FTransform Orientation, float AngleDeg, float Extent, bool bBidirectional)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptTwistWarpOptions Options;
	Options.bBidirectional = bBidirectional;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyTwistWarpToMesh(Mesh, Options, Orientation, AngleDeg, Extent, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Twisted %.1f degrees"), AngleDeg));
}

FModelingResult UModelingService::Flare(int32 Handle, FTransform Orientation, float PercentX, float PercentY, float Extent)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptFlareWarpOptions Options;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyFlareWarpToMesh(Mesh, Options, Orientation, PercentX, PercentY, Extent, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Flared"));
}

FModelingResult UModelingService::Noise(int32 Handle, const FString& SelectionName, float Magnitude, float Frequency, int32 RandomSeed)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptPerlinNoiseOptions Options;
	Options.BaseLayer.Magnitude = Magnitude;
	Options.BaseLayer.Frequency = Frequency;
	Options.BaseLayer.RandomSeed = RandomSeed;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyPerlinNoiseToMesh2(Mesh, Selection, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Applied noise (magnitude %.2f, frequency %.3f)"), Magnitude, Frequency));
}

FModelingResult UModelingService::DisplaceFromTexture(int32 Handle, const FString& SelectionName, const FString& TexturePath, float Magnitude, int32 UVLayer)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UTexture2D* Texture = LoadAssetAs<UTexture2D>(TexturePath);
	if (!Texture) { return Fail(Handle, FString::Printf(TEXT("Texture2D not found: %s"), *TexturePath)); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptDisplaceFromTextureOptions Options;
	Options.Magnitude = Magnitude;
	FGeometryScriptAdaptiveTessellationOptions TessellationOptions;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshDeformFunctions::ApplyDisplaceFromTextureMap(Mesh, Texture, Selection, Options, TessellationOptions, UVLayer, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Displaced from %s"), *TexturePath));
}

// =========================================================================
// Voxel operations
// =========================================================================

FModelingResult UModelingService::VoxelSolidify(int32 Handle, int32 GridResolution, float WindingThreshold)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptSolidifyOptions Options;
	Options.GridParameters.SizeMethod = EGeometryScriptGridSizingMethod::GridResolution;
	Options.GridParameters.GridResolution = FMath::Clamp(GridResolution, 8, 512);
	Options.WindingThreshold = WindingThreshold;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshVoxelFunctions::ApplyMeshSolidify(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Solidified at grid %d"), GridResolution));
}

FModelingResult UModelingService::VoxelMorphology(int32 Handle, const FString& Operation, float Distance, int32 GridResolution)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMorphologyOptions Options;
	if (!ParseEnum(Operation, Options.Operation))
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown morphology operation '%s' (use %s)"), *Operation, *EnumNames<EGeometryScriptMorphologicalOpType>()));
	}
	Options.Distance = Distance;
	Options.SDFGridParameters.SizeMethod = EGeometryScriptGridSizingMethod::GridResolution;
	Options.SDFGridParameters.GridResolution = FMath::Clamp(GridResolution, 8, 512);
	Options.MeshGridParameters = Options.SDFGridParameters;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshVoxelFunctions::ApplyMeshMorphology(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Morphology %s by %.2f"), *Operation, Distance));
}

// =========================================================================
// UVs, normals, groups, materials, colors
// =========================================================================

FModelingResult UModelingService::AutoUV(int32 Handle, const FString& Method, int32 UVLayer)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	// Booleans, deletes, and cuts leave id gaps; XAtlas refuses a non-compact mesh, so compact first.
	UGeometryScriptLibrary_MeshRepairFunctions::CompactMesh(Mesh, Debug);
	if (GetMeshInfo(Handle).NumUVLayers <= UVLayer)
	{
		UGeometryScriptLibrary_MeshUVFunctions::SetNumUVSets(Mesh, UVLayer + 1, Debug);
	}
	if (Method.Equals(TEXT("XAtlas"), ESearchCase::IgnoreCase))
	{
		FGeometryScriptXAtlasOptions Options;
		UGeometryScriptLibrary_MeshUVFunctions::AutoGenerateXAtlasMeshUVs(Mesh, UVLayer, Options, Debug);
	}
	else if (Method.Equals(TEXT("PatchBuilder"), ESearchCase::IgnoreCase))
	{
		FGeometryScriptPatchBuilderOptions Options;
		UGeometryScriptLibrary_MeshUVFunctions::AutoGeneratePatchBuilderMeshUVs(Mesh, UVLayer, Options, Debug);
	}
	else
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown UV method '%s' (use XAtlas or PatchBuilder)"), *Method));
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Generated UVs (%s) in layer %d"), *Method, UVLayer));
}

FModelingResult UModelingService::ProjectUV(int32 Handle, const FString& Method, FTransform ProjectorTransform, int32 UVLayer, const FString& SelectionName)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	if (GetMeshInfo(Handle).NumUVLayers <= UVLayer)
	{
		UGeometryScriptLibrary_MeshUVFunctions::SetNumUVSets(Mesh, UVLayer + 1, Debug);
	}
	if (Method.Equals(TEXT("Planar"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromPlanarProjection(Mesh, UVLayer, ProjectorTransform, Selection, Debug);
	}
	else if (Method.Equals(TEXT("Box"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromBoxProjection(Mesh, UVLayer, ProjectorTransform, Selection, 2, Debug);
	}
	else if (Method.Equals(TEXT("Cylinder"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromCylinderProjection(Mesh, UVLayer, ProjectorTransform, Selection, 45.f, Debug);
	}
	else
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown projection '%s' (use Planar, Box, Cylinder)"), *Method));
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Projected %s UVs into layer %d"), *Method, UVLayer));
}

FModelingResult UModelingService::RepackUV(int32 Handle, int32 UVLayer, int32 TextureResolution)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptRepackUVsOptions Options;
	Options.TargetImageWidth = FMath::Max(64, TextureResolution);
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshUVFunctions::RepackMeshUVs(Mesh, UVLayer, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Repacked UV layer %d"), UVLayer));
}

FModelingResult UModelingService::SetNumUVLayers(int32 Handle, int32 NumLayers)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshUVFunctions::SetNumUVSets(Mesh, FMath::Clamp(NumLayers, 1, 8), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Set %d UV layers"), NumLayers));
}

FModelingResult UModelingService::RecomputeNormals(int32 Handle, float HardAngleDeg)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptCalculateNormalsOptions CalculateOptions;
	if (HardAngleDeg < 0.f)
	{
		UGeometryScriptLibrary_MeshNormalsFunctions::SetPerVertexNormals(Mesh, Debug);
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh, CalculateOptions, false, Debug);
		return Finish(Handle, Mesh, Debug, TEXT("Recomputed smooth normals"));
	}
	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = HardAngleDeg;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(Mesh, SplitOptions, CalculateOptions, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Recomputed normals (hard edges over %.1f degrees)"), HardAngleDeg));
}

FModelingResult UModelingService::FlipNormals(int32 Handle)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshNormalsFunctions::FlipNormals(Mesh, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Flipped normals"));
}

FModelingResult UModelingService::ComputePolygroups(int32 Handle, const FString& Method, float CreaseAngleDeg, int32 MinGroupSize)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	const FGeometryScriptGroupLayer Layer;
	if (Method.Equals(TEXT("Angle"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshPolygroupFunctions::ComputePolygroupsFromAngleThreshold(Mesh, Layer, CreaseAngleDeg, MinGroupSize, Debug);
	}
	else if (Method.Equals(TEXT("UVIslands"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshPolygroupFunctions::ConvertUVIslandsToPolygroups(Mesh, Layer, 0, Debug);
	}
	else if (Method.Equals(TEXT("Components"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshPolygroupFunctions::ConvertComponentsToPolygroups(Mesh, Layer, Debug);
	}
	else if (Method.Equals(TEXT("Polygons"), ESearchCase::IgnoreCase))
	{
		UGeometryScriptLibrary_MeshPolygroupFunctions::ComputePolygroupsFromPolygonDetection(Mesh, Layer, true, false, 1.0, 1.0, 1, Debug);
	}
	else
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown polygroup method '%s' (use Angle, UVIslands, Components, Polygons)"), *Method));
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Computed polygroups (%s)"), *Method));
}

FModelingResult UModelingService::RemapMaterialID(int32 Handle, int32 FromMaterialID, int32 ToMaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshMaterialFunctions::EnableMaterialIDs(Mesh, Debug);
	UGeometryScriptLibrary_MeshMaterialFunctions::RemapMaterialIDs(Mesh, FromMaterialID, ToMaterialID, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Remapped material %d -> %d"), FromMaterialID, ToMaterialID));
}

FModelingResult UModelingService::SetMaterialID(int32 Handle, const FString& SelectionName, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshMaterialFunctions::EnableMaterialIDs(Mesh, Debug);
	UGeometryScriptLibrary_MeshMaterialFunctions::SetMaterialIDForMeshSelection(Mesh, Selection, MaterialID, false, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Set material ID %d on %d triangles"), MaterialID, Selection.GetNumSelected()));
}

FModelingResult UModelingService::SetVertexColor(int32 Handle, const FString& SelectionName, FLinearColor Color)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptColorFlags Flags;
	if (SelectionName.IsEmpty())
	{
		UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshConstantVertexColor(Mesh, Color, Flags, false, Debug);
		return Finish(Handle, Mesh, Debug, TEXT("Set constant vertex color"));
	}
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	if (!GetMeshInfo(Handle).bHasVertexColors)
	{
		UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshConstantVertexColor(Mesh, FLinearColor::White, Flags, false, Debug);
	}
	UGeometryScriptLibrary_MeshVertexColorFunctions::SetMeshSelectionVertexColor(Mesh, Selection, Color, Flags, false, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Set vertex color on selection '%s'"), *SelectionName));
}

// =========================================================================
// Transform
// =========================================================================

FModelingResult UModelingService::TransformMesh(int32 Handle, FTransform Transform)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshTransformFunctions::TransformMesh(Mesh, Transform, true, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Transformed"));
}

FModelingResult UModelingService::TranslateMesh(int32 Handle, FVector Translation)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshTransformFunctions::TranslateMesh(Mesh, Translation, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Translated"));
}

FModelingResult UModelingService::RotateMesh(int32 Handle, FRotator Rotation, FVector Origin)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshTransformFunctions::RotateMesh(Mesh, Rotation, Origin, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Rotated"));
}

FModelingResult UModelingService::ScaleMesh(int32 Handle, FVector Scale, FVector Origin)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshTransformFunctions::ScaleMesh(Mesh, Scale, Origin, true, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Scaled"));
}

FModelingResult UModelingService::RecenterMesh(int32 Handle, const FString& Mode)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	const FBox Bounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(Mesh);
	FVector Shift;
	if (Mode.Equals(TEXT("Bounds"), ESearchCase::IgnoreCase))
	{
		Shift = -Bounds.GetCenter();
	}
	else if (Mode.Equals(TEXT("Base"), ESearchCase::IgnoreCase))
	{
		const FVector Center = Bounds.GetCenter();
		Shift = FVector(-Center.X, -Center.Y, -Bounds.Min.Z);
	}
	else
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown recenter mode '%s' (use Bounds or Base)"), *Mode));
	}
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshTransformFunctions::TranslateMesh(Mesh, Shift, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Recentered (%s), shifted by %s"), *Mode, *Shift.ToCompactString()));
}

// =========================================================================
// Assets, collision, LODs, baking, placement
// =========================================================================

FModelingResult UModelingService::SaveMeshToStaticMesh(int32 Handle, const FString& AssetPath, bool bReplaceExisting, bool bEnableCollision,
	bool bEnableNanite, bool bSaveAsset)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (TriCount(Mesh) == 0) { return Fail(Handle, TEXT("Mesh is empty; nothing to save")); }
	if (!AssetPath.StartsWith(TEXT("/"))) { return Fail(Handle, TEXT("AssetPath must be a content path like /Game/Props/SM_Crate")); }

	UGeometryScriptDebug* Debug = NewDebug();
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UStaticMesh* StaticMesh = nullptr;
	FString What;
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		StaticMesh = LoadAssetAs<UStaticMesh>(AssetPath);
		if (!StaticMesh) { return Fail(Handle, FString::Printf(TEXT("%s exists but is not a StaticMesh"), *AssetPath)); }
		if (!bReplaceExisting) { return Fail(Handle, FString::Printf(TEXT("%s already exists (pass replace_existing=True to overwrite LOD0)"), *AssetPath)); }
		FGeometryScriptCopyMeshToAssetOptions Options;
		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.LODIndex = 0;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(Mesh, StaticMesh, Options, WriteLOD, Outcome, true, Debug);
		What = FString::Printf(TEXT("Replaced LOD0 of %s"), *AssetPath);
	}
	else
	{
		FGeometryScriptCreateNewStaticMeshAssetOptions Options;
		Options.bEnableCollision = bEnableCollision;
		Options.bEnableNanite = bEnableNanite;
		StaticMesh = UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewStaticMeshAssetFromMesh(Mesh, AssetPath, Options, Outcome, Debug);
		What = FString::Printf(TEXT("Created %s"), *AssetPath);
	}
	if (Outcome != EGeometryScriptOutcomePins::Success || !StaticMesh)
	{
		return Fail(Handle, FString::Printf(TEXT("Saving to %s failed: %s"), *AssetPath, *DebugText(Debug)));
	}
	SaveIf(bSaveAsset, StaticMesh->GetPathName());
	FModelingResult R = Ok(Handle, Mesh, What);
	R.AssetPath = StaticMesh->GetPathName();
	return R;
}

namespace
{
	/**
	 * Check a session mesh against a skeletal mesh asset, then write it to one LOD in place. Kept: the skeleton, the asset's
	 * material slots and LOD material map, every other LOD, this LOD's normal/tangent build settings and its polygon-group
	 * slot names (the engine writer leaves those empty, which can move sections onto the wrong material). Refused before
	 * anything is touched: no weights, unweighted vertices, bones the skeleton lacks, bone or material indices out of range.
	 */
	bool WriteSkeletalLOD(int32 Handle, UDynamicMesh* Mesh, USkeletalMesh* SkeletalMesh, int32 LODIndex, FString& OutMessage, bool bKeepNormalSettings = true)
	{
		const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
		const int32 NumRefBones = RefSkeleton.GetRawBoneNum();
		const int32 NumLODs = SkeletalMesh->GetLODNum();
		if (LODIndex < 0 || LODIndex > NumLODs)
		{
			OutMessage = FString::Printf(TEXT("%s has %d LOD(s): write LOD 0..%d (LOD %d appends a new one)"), *SkeletalMesh->GetPathName(), NumLODs, NumLODs, NumLODs);
			return false;
		}
		if (Session().Poses.Contains(Handle))
		{
			OutMessage = FString::Printf(TEXT("Handle %d holds a POSED mesh (%s): call unpose_mesh(handle) first so the reference pose is written"), Handle, *Session().Poses[Handle].Source);
			return false;
		}

		bool bHasWeights = false;
		bool bHasBones = false;
		int32 NumMeshBones = 0;
		int32 Unweighted = 0;
		int32 MaxBoneIndex = -1;
		int32 MaxMaterialID = 0;
		TArray<FString> MissingBones;
		Mesh->ProcessMesh([&](const FDynamicMesh3& M)
		{
			if (!M.HasAttributes()) { return; }
			const FDynamicMeshAttributeSet* Attributes = M.Attributes();
			bHasBones = Attributes->HasBones() && Attributes->GetNumBones() > 0;
			if (bHasBones)
			{
				NumMeshBones = Attributes->GetNumBones();
				for (int32 i = 0; i < NumMeshBones; ++i)
				{
					const FName Name = Attributes->GetBoneNames()->GetValue(i);
					if (RefSkeleton.FindRawBoneIndex(Name) == INDEX_NONE) { MissingBones.AddUnique(Name.ToString()); }
				}
			}
			if (const FDynamicMeshVertexSkinWeightsAttribute* Skin = Attributes->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName))
			{
				bHasWeights = true;
				UE::AnimationCore::FBoneWeights Weights;
				for (const int32 Vid : M.VertexIndicesItr())
				{
					Skin->GetValue(Vid, Weights);
					float Sum = 0.f;
					for (int32 k = 0; k < Weights.Num(); ++k)
					{
						Sum += Weights[k].GetWeight();
						MaxBoneIndex = FMath::Max(MaxBoneIndex, static_cast<int32>(Weights[k].GetBoneIndex()));
					}
					if (Sum <= 1e-4f) { ++Unweighted; }
				}
			}
			if (Attributes->HasMaterialID())
			{
				for (const int32 Tid : M.TriangleIndicesItr()) { MaxMaterialID = FMath::Max(MaxMaterialID, Attributes->GetMaterialID()->GetValue(Tid)); }
			}
		});
		if (!bHasWeights)
		{
			OutMessage = TEXT("Mesh has no skin weights: transfer_bone_weights / skin_to_surface / bind_selection_to_bone first");
			return false;
		}
		if (Unweighted > 0)
		{
			OutMessage = FString::Printf(TEXT("%d vertices carry no bone weight and would collapse onto the root at runtime: weight them first (transfer_bone_weights, skin_to_surface, bind_selection_to_bone)"), Unweighted);
			return false;
		}
		if (MissingBones.Num() > 0)
		{
			OutMessage = FString::Printf(TEXT("The mesh has bone(s) the skeleton of %s lacks: %s"), *SkeletalMesh->GetPathName(), *FString::Join(MissingBones, TEXT(", ")));
			return false;
		}
		const int32 BoneLimit = bHasBones ? NumMeshBones : NumRefBones;
		if (MaxBoneIndex >= BoneLimit)
		{
			OutMessage = FString::Printf(TEXT("Skin weights reference bone index %d but only %d bone(s) exist"), MaxBoneIndex, BoneLimit);
			return false;
		}

		// Polygon groups of the LOD as it is now: material ID k is polygon group k on both read and write.
		TMap<int32, FName> GroupSlotNames;
		int32 NumGroups = 0;
		if (LODIndex < NumLODs && SkeletalMesh->HasMeshDescription(LODIndex))
		{
			if (const FMeshDescription* OldDescription = SkeletalMesh->GetMeshDescription(LODIndex))
			{
				const TPolygonGroupAttributesConstRef<FName> Names = OldDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
				for (const FPolygonGroupID Group : OldDescription->PolygonGroups().GetElementIDs())
				{
					NumGroups = FMath::Max(NumGroups, Group.GetValue() + 1);
					if (Names.IsValid()) { GroupSlotNames.Add(Group.GetValue(), Names[Group]); }
				}
			}
		}
		const int32 NumSlots = FMath::Max(NumGroups, SkeletalMesh->GetMaterials().Num());
		if (MaxMaterialID >= NumSlots)
		{
			OutMessage = FString::Printf(TEXT("Material ID %d has no slot on %s (%d slot(s)); remap_material_id it onto an existing one, or set_asset_materials first"), MaxMaterialID, *SkeletalMesh->GetPathName(), NumSlots);
			return false;
		}

		// Unchanged topology since the load: keep the asset's vertex order (morph targets and anything else indexed by vertex).
		bool bSameTopology = false;
		if (const FSkeletalOrigin* Origin = Session().Origins.Find(Handle))
		{
			bSameTopology = FPackageName::ObjectPathToPackageName(Origin->AssetPath) == SkeletalMesh->GetOutermost()->GetName() && Origin->LODIndex == LODIndex && !Origin->bFromRenderData
				&& Origin->VertexCount == VertCount(Mesh) && Origin->TriangleCount == TriCount(Mesh);
		}

		const FSkeletalMeshLODInfo* LODInfo = LODIndex < NumLODs ? SkeletalMesh->GetLODInfo(LODIndex) : nullptr;
		FGeometryScriptCopyMeshToAssetOptions Options;
		// bKeepNormalSettings false = the handle's normals and tangents are stored as they are (what save_mesh_to_skeletal_mesh
		// has always done when replacing LOD0); true = the LOD keeps its own build settings, which may recompute them.
		Options.bEnableRecomputeNormals = bKeepNormalSettings && LODInfo ? LODInfo->BuildSettings.bRecomputeNormals : false;
		Options.bEnableRecomputeTangents = bKeepNormalSettings && LODInfo ? LODInfo->BuildSettings.bRecomputeTangents : false;
		const bool bAssetRecomputesNormals = Options.bEnableRecomputeNormals;
		Options.BoneHierarchyMismatchHandling = bHasBones ? EGeometryScriptBoneHierarchyMismatchHandling::RemapGeometryToReferenceSkeleton : EGeometryScriptBoneHierarchyMismatchHandling::DoNothing;
		Options.bUseOriginalVertexOrder = bSameTopology;
		Options.bReplaceMaterials = false;
		Options.bEmitTransaction = false;
		Options.bDeferMeshPostEditChange = true;
		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.LODIndex = LODIndex;

		FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Vibe3D: write LOD %d of %s"), LODIndex, *SkeletalMesh->GetName())));
		SkeletalMesh->PreEditChange(nullptr);
		UGeometryScriptDebug* Debug = NewDebug();
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToSkeletalMesh(Mesh, SkeletalMesh, Options, WriteLOD, Outcome, Debug);
		int32 RestoredNames = 0;
		if (Outcome == EGeometryScriptOutcomePins::Success && GroupSlotNames.Num() > 0)
		{
			if (FMeshDescription* NewDescription = SkeletalMesh->GetMeshDescription(LODIndex))
			{
				TPolygonGroupAttributesRef<FName> Names = NewDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
				if (Names.IsValid())
				{
					for (const FPolygonGroupID Group : NewDescription->PolygonGroups().GetElementIDs())
					{
						const FName* Old = GroupSlotNames.Find(Group.GetValue());
						if (Old && !Old->IsNone() && Names[Group] != *Old) { Names[Group] = *Old; ++RestoredNames; }
					}
					if (RestoredNames > 0) { SkeletalMesh->CommitMeshDescription(LODIndex); }
				}
			}
		}
		SkeletalMesh->PostEditChange();
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutMessage = FString::Printf(TEXT("Writing LOD %d of %s failed: %s"), LODIndex, *SkeletalMesh->GetPathName(), *DebugText(Debug));
			return false;
		}
		FAssetCompilingManager::Get().FinishAllCompilation();

		// Read the LOD back and compare: the vertex count the asset now holds must be the one that was written.
		UDynamicMesh* Check = NewScratchMesh();
		UGeometryScriptDebug* CheckDebug = NewDebug();
		const int32 Written = CopyFromSkeletalMesh(SkeletalMesh, Check, LODIndex, CheckDebug) ? VertCount(Check) : -1;
		if (Written != VertCount(Mesh))
		{
			OutMessage = FString::Printf(TEXT("Wrote LOD %d of %s but reading it back gives %d vertices instead of %d: check the asset"), LODIndex, *SkeletalMesh->GetPathName(), Written, VertCount(Mesh));
			return false;
		}
		OutMessage = FString::Printf(TEXT("%s LOD %d of %s: %d verts, %d tris, all weighted, %s; skeleton, material slots and the other %d LOD(s) kept%s%s"),
			LODIndex == NumLODs ? TEXT("Added") : TEXT("Wrote"), LODIndex, *SkeletalMesh->GetPathName(), Written, TriCount(Mesh),
			bHasBones ? *FString::Printf(TEXT("%d bone(s) matched to the skeleton by name"), NumMeshBones) : TEXT("bone indices taken as skeleton indices"),
			FMath::Max(0, SkeletalMesh->GetLODNum() - 1),
			bSameTopology ? TEXT(", original vertex order") : TEXT(""),
			RestoredNames ? *FString::Printf(TEXT(", %d section slot name(s) restored"), RestoredNames) : TEXT(""));
		if (bAssetRecomputesNormals)
		{
			OutMessage += TEXT("; this LOD's build settings recompute normals, so the handle's own normals are not used");
		}
		return true;
	}
}

FModelingResult UModelingService::SaveMeshToSkeletalMeshLOD(int32 Handle, const FString& AssetPath, int32 LODIndex, bool bSaveAsset)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (TriCount(Mesh) == 0) { return Fail(Handle, TEXT("Mesh is empty; nothing to write")); }
	const FSkeletalOrigin* Origin = Session().Origins.Find(Handle);
	const FString Path = AssetPath.IsEmpty() && Origin ? Origin->AssetPath : AssetPath;
	const int32 LOD = LODIndex < 0 && Origin ? Origin->LODIndex : LODIndex;
	if (Path.IsEmpty() || LOD < 0)
	{
		return Fail(Handle, FString::Printf(TEXT("Handle %d was not loaded with load_mesh_from_skeletal_mesh: pass asset_path and lod_index explicitly"), Handle));
	}
	USkeletalMesh* SkeletalMesh = LoadAssetAs<USkeletalMesh>(Path);
	if (!SkeletalMesh) { return Fail(Handle, FString::Printf(TEXT("SkeletalMesh not found: %s"), *Path)); }
	FString Message;
	if (!WriteSkeletalLOD(Handle, Mesh, SkeletalMesh, LOD, Message)) { return Fail(Handle, Message); }
	if (FSkeletalOrigin* Mutable = Session().Origins.Find(Handle))
	{
		// The handle now mirrors what the asset holds; a further write keeps the original vertex order again.
		Mutable->AssetPath = Path;
		Mutable->LODIndex = LOD;
		Mutable->VertexCount = VertCount(Mesh);
		Mutable->TriangleCount = TriCount(Mesh);
		Mutable->bFromRenderData = false;
	}
	SaveIf(bSaveAsset, Path);
	FModelingResult R = Ok(Handle, Mesh, Message);
	R.AssetPath = Path;
	return R;
}

FModelingResult UModelingService::InspectSkeletalMesh(const FString& AssetPath)
{
	USkeletalMesh* SkeletalMesh = LoadAssetAs<USkeletalMesh>(AssetPath);
	if (!SkeletalMesh) { return Fail(-1, FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath)); }
	FAssetCompilingManager::Get().FinishAllCompilation();
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	FString Lines = FString::Printf(TEXT("%s: skeleton %s, %d bone(s), %d LOD(s), %d material slot(s)"), *AssetPath,
		SkeletalMesh->GetSkeleton() ? *SkeletalMesh->GetSkeleton()->GetPathName() : TEXT("(none)"), RefSkeleton.GetRawBoneNum(), SkeletalMesh->GetLODNum(), SkeletalMesh->GetMaterials().Num());
	const TArray<FSkeletalMaterial>& Slots = SkeletalMesh->GetMaterials();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		Lines += FString::Printf(TEXT("\n  slot %d '%s': %s"), i, *Slots[i].MaterialSlotName.ToString(), Slots[i].MaterialInterface ? *Slots[i].MaterialInterface->GetPathName() : TEXT("None"));
	}
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	int32 LOD0Verts = 0, LOD0Tris = 0;
	for (int32 LOD = 0; LOD < SkeletalMesh->GetLODNum(); ++LOD)
	{
		UDynamicMesh* Scratch = NewScratchMesh();
		UGeometryScriptDebug* Debug = NewDebug();
		const bool bRead = CopyFromSkeletalMesh(SkeletalMesh, Scratch, LOD, Debug);
		const int32 Verts = bRead ? VertCount(Scratch) : 0;
		const int32 Tris = bRead ? TriCount(Scratch) : 0;
		if (LOD == 0) { LOD0Verts = Verts; LOD0Tris = Tris; }
		const FSkeletalMeshLODInfo* Info = SkeletalMesh->GetLODInfo(LOD);
		const int32 Required = (RenderData && RenderData->LODRenderData.IsValidIndex(LOD)) ? RenderData->LODRenderData[LOD].RequiredBones.Num() : -1;
		Lines += FString::Printf(TEXT("\n  LOD %d: %s, %d verts, %d tris, %d bone(s) used at runtime, screen size %.2f%s%s"), LOD,
			SkeletalMesh->HasMeshDescription(LOD) ? TEXT("source model") : TEXT("generated (render data only)"), Verts, Tris, Required,
			Info ? Info->ScreenSize.Default : 0.f,
			Info && Info->LODMaterialMap.Num() ? *FString::Printf(TEXT(", material map [%s]"), *FString::JoinBy(Info->LODMaterialMap, TEXT(","), [](int32 V) { return FString::FromInt(V); })) : TEXT(""),
			Info && Info->BonesToRemove.Num() ? *FString::Printf(TEXT(", %d bone(s) removed"), Info->BonesToRemove.Num()) : TEXT(""));
	}
	FModelingResult R;
	R.bSuccess = true;
	R.AssetPath = AssetPath;
	R.VertexCount = LOD0Verts;
	R.TriangleCount = LOD0Tris;
	R.Message = Lines;
	return R;
}

FModelingResult UModelingService::SaveMeshToSkeletalMesh(int32 Handle, const FString& AssetPath, const FString& SkeletonPath, bool bReplaceExisting, bool bSaveAsset)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (TriCount(Mesh) == 0) { return Fail(Handle, TEXT("Mesh is empty; nothing to save")); }
	if (!AssetPath.StartsWith(TEXT("/"))) { return Fail(Handle, FString::Printf(TEXT("AssetPath must be a full package path like /Game/Folder/SK_Name (got '%s')"), *AssetPath)); }

	bool bMeshHasBones = false;
	bool bMeshHasWeights = false;
	int32 MaxMaterialID = 0;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		if (!M.HasAttributes()) { return; }
		bMeshHasBones = M.Attributes()->HasBones();
		bMeshHasWeights = M.Attributes()->GetSkinWeightsAttributes().Num() > 0;
		if (M.Attributes()->HasMaterialID())
		{
			const FDynamicMeshMaterialAttribute* MaterialIDs = M.Attributes()->GetMaterialID();
			for (const int32 Tid : M.TriangleIndicesItr()) { MaxMaterialID = FMath::Max(MaxMaterialID, MaterialIDs->GetValue(Tid)); }
		}
	});
	if (!bMeshHasWeights)
	{
		return Fail(Handle, TEXT("Mesh has no skin weights: run create_bones + bind_selection_to_bone (or transfer_bone_weights from a skeletal mesh) first"));
	}

	const bool bExists = UEditorAssetLibrary::DoesAssetExist(AssetPath);
	USkeleton* Skeleton = nullptr;
	// Only a skeleton this call created (or its own <AssetPath>_Skeleton) is ours to modify and save. A skeleton passed in by path is
	// usually shared (an engine MetaHuman skeleton, a game skeleton): binding a new mesh to it must never re-save it.
	bool bOwnSkeleton = false;
	FString SkeletonNote;
	if (!SkeletonPath.IsEmpty())
	{
		Skeleton = LoadAssetAs<USkeleton>(SkeletonPath);
		if (!Skeleton) { return Fail(Handle, FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath)); }
	}
	else if (!bExists)
	{
		if (!bMeshHasBones)
		{
			return Fail(Handle, TEXT("No SkeletonPath and the mesh has no bones: pass an existing skeleton, or define bones with create_bones so one can be created"));
		}
		// Create an empty Skeleton next to the mesh; CreateSkeletalMeshAsset merges the mesh's bones into it.
		const FString NewSkeletonPath = AssetPath + TEXT("_Skeleton");
		if (UEditorAssetLibrary::DoesAssetExist(NewSkeletonPath))
		{
			Skeleton = LoadAssetAs<USkeleton>(NewSkeletonPath);
			if (!Skeleton) { return Fail(Handle, FString::Printf(TEXT("%s exists but is not a Skeleton"), *NewSkeletonPath)); }
		}
		else
		{
			UPackage* Package = CreatePackage(*NewSkeletonPath);
			Skeleton = NewObject<USkeleton>(Package, *FPackageName::GetLongPackageAssetName(NewSkeletonPath), RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Skeleton);
			Package->MarkPackageDirty();
		}
		bOwnSkeleton = true;
		SkeletonNote = FString::Printf(TEXT(" with skeleton %s"), *NewSkeletonPath);
	}

	UGeometryScriptDebug* Debug = NewDebug();
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	USkeletalMesh* SkeletalMesh = nullptr;
	FString What;
	if (bExists)
	{
		SkeletalMesh = LoadAssetAs<USkeletalMesh>(AssetPath);
		if (!SkeletalMesh) { return Fail(Handle, FString::Printf(TEXT("%s exists but is not a SkeletalMesh"), *AssetPath)); }
		if (!bReplaceExisting) { return Fail(Handle, FString::Printf(TEXT("%s already exists (pass replace_existing=True to overwrite LOD0)"), *AssetPath)); }
		// Same validated writer as save_mesh_to_skeletal_mesh_lod: skeleton, slots, other LODs and section slot names kept.
		FString WriteMessage;
		// The handle's normals and tangents are stored as they are, as this call has always done.
		if (!WriteSkeletalLOD(Handle, Mesh, SkeletalMesh, 0, WriteMessage, false)) { return Fail(Handle, WriteMessage); }
		Outcome = EGeometryScriptOutcomePins::Success;
		What = WriteMessage;
	}
	else
	{
		FGeometryScriptCreateNewSkeletalMeshAssetOptions Options;
		// Bones authored on the mesh (create_bones, or copied from a source skeletal mesh) define the reference skeleton.
		Options.bUseMeshBoneProportions = bMeshHasBones;
		// One slot per material ID so multi-material meshes keep their sections; set_asset_materials fills them in.
		for (int32 i = 0; i <= MaxMaterialID; ++i)
		{
			Options.Materials.Add(*FString::Printf(TEXT("Slot%d"), i), UMaterial::GetDefaultMaterial(MD_Surface));
		}
		SkeletalMesh = UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewSkeletalMeshAssetFromMesh(Mesh, Skeleton, AssetPath, Options, Outcome, Debug);
		if (SkeletalMesh && Skeleton && bOwnSkeleton && !Skeleton->GetPreviewMesh())
		{
			Skeleton->SetPreviewMesh(SkeletalMesh);
		}
		What = FString::Printf(TEXT("Created %s%s (%d material slot(s))"), *AssetPath, *SkeletonNote, MaxMaterialID + 1);
	}
	if (Outcome != EGeometryScriptOutcomePins::Success || !SkeletalMesh)
	{
		return Fail(Handle, FString::Printf(TEXT("Saving to %s failed: %s"), *AssetPath, *DebugText(Debug)));
	}
	SaveIf(bSaveAsset, SkeletalMesh->GetPathName());
	if (Skeleton && bOwnSkeleton) { SaveIf(bSaveAsset, Skeleton->GetPathName()); }
	FModelingResult R = Ok(Handle, Mesh, What);
	R.AssetPath = SkeletalMesh->GetPathName();
	return R;
}

FModelingResult UModelingService::TransferBoneWeights(int32 Handle, const FString& SourceSkeletalMeshPath, int32 SourceLODIndex)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	USkeletalMesh* SkeletalMesh = LoadAssetAs<USkeletalMesh>(SourceSkeletalMeshPath);
	if (!SkeletalMesh) { return Fail(Handle, FString::Printf(TEXT("SkeletalMesh not found: %s"), *SourceSkeletalMeshPath)); }
	UGeometryScriptDebug* Debug = NewDebug();
	UDynamicMesh* Source = NewScratchMesh();
	if (!CopyFromSkeletalMesh(SkeletalMesh, Source, SourceLODIndex, Debug))
	{
		return Fail(Handle, FString::Printf(TEXT("Could not read %s: %s"), *SourceSkeletalMeshPath, *DebugText(Debug)));
	}
	UGeometryScriptLibrary_MeshBoneWeightFunctions::TransferBoneWeightsFromMesh(Source, Mesh, FGeometryScriptTransferBoneWeightsOptions(), FGeometryScriptMeshSelection(), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Transferred bone weights from %s"), *SourceSkeletalMeshPath));
}

FModelingResult UModelingService::GenerateCollision(const FString& AssetPath, const FString& Method, int32 MaxConvexHulls, int32 ConvexHullTargetFaceCount, bool bSaveAsset)
{
	UStaticMesh* StaticMesh = LoadAssetAs<UStaticMesh>(AssetPath);
	if (!StaticMesh) { return Fail(-1, FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath)); }
	FGeometryScriptCollisionFromMeshOptions Options;
	if (!ParseEnum(Method, Options.Method))
	{
		return Fail(-1, FString::Printf(TEXT("Unknown collision method '%s' (use %s)"), *Method, *EnumNames<EGeometryScriptCollisionGenerationMethod>()));
	}
	Options.MaxConvexHullsPerMesh = FMath::Max(1, MaxConvexHulls);
	Options.ConvexHullTargetFaceCount = FMath::Max(4, ConvexHullTargetFaceCount);
	UGeometryScriptDebug* Debug = NewDebug();
	UDynamicMesh* Scratch = NewScratchMesh();
	if (!CopyFromStaticMesh(StaticMesh, Scratch, 0, Debug))
	{
		return Fail(-1, FString::Printf(TEXT("Could not read %s: %s"), *AssetPath, *DebugText(Debug)));
	}
	UGeometryScriptLibrary_CollisionFunctions::SetStaticMeshCollisionFromMesh(Scratch, StaticMesh, Options, FGeometryScriptSetStaticMeshCollisionOptions(), Debug);
	if (DebugHasErrors(Debug))
	{
		return Fail(-1, FString::Printf(TEXT("Collision generation failed: %s"), *DebugText(Debug)));
	}
	SaveIf(bSaveAsset, AssetPath);
	FModelingResult R = Ok(-1, Scratch, FString::Printf(TEXT("Generated %s collision on %s"), *Method, *AssetPath));
	R.AssetPath = AssetPath;
	return R;
}

FModelingResult UModelingService::SetLODs(const FString& AssetPath, const TArray<float>& PercentTrianglesPerLOD, bool bAutoComputeScreenSize, bool bSaveAsset)
{
	UStaticMesh* StaticMesh = LoadAssetAs<UStaticMesh>(AssetPath);
	if (!StaticMesh) { return Fail(-1, FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath)); }
	if (PercentTrianglesPerLOD.Num() == 0) { return Fail(-1, TEXT("Give one PercentTriangles entry per LOD, e.g. [1.0, 0.5, 0.25]")); }
	UStaticMeshEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
	if (!Subsystem) { return Fail(-1, TEXT("StaticMeshEditorSubsystem unavailable")); }
	FStaticMeshReductionOptions Options;
	Options.bAutoComputeLODScreenSize = bAutoComputeScreenSize;
	for (int32 i = 0; i < PercentTrianglesPerLOD.Num(); ++i)
	{
		FStaticMeshReductionSettings Settings;
		Settings.PercentTriangles = FMath::Clamp(PercentTrianglesPerLOD[i], 0.01f, 1.f);
		Settings.ScreenSize = 1.f / FMath::Pow(2.f, static_cast<float>(i));
		Options.ReductionSettings.Add(Settings);
	}
	const int32 NumLODs = Subsystem->SetLods(StaticMesh, Options);
	if (NumLODs < 0)
	{
		return Fail(-1, FString::Printf(TEXT("SetLods failed on %s"), *AssetPath));
	}
	SaveIf(bSaveAsset, AssetPath);
	FModelingResult R;
	R.bSuccess = true;
	R.AssetPath = AssetPath;
	R.Message = FString::Printf(TEXT("%s now has %d LODs"), *AssetPath, NumLODs);
	return R;
}

// =========================================================================
// Generated-prop intake
// =========================================================================

namespace
{
	const FName IntakeFingerprintTag(TEXT("Vibe3DIntakeFingerprint"));
	const FName IntakeSourceTag(TEXT("Vibe3DIntakeSource"));

	FModelingIntakeReport IntakeFail(const FString& AssetPath, const FString& Message)
	{
		FModelingIntakeReport R;
		R.AssetPath = AssetPath;
		R.Message = Message;
		UE_LOG(LogVibe3DModeling, Warning, TEXT("IntakeGeneratedMesh failure (%s): %s"), *AssetPath, *Message);
		return R;
	}

	/** The asset-side half of a report: counts, bounds, LODs, collision, slots and the textures those slots use. */
	void DescribeIntakeAsset(UStaticMesh* StaticMesh, FModelingIntakeReport& R)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		R.TriangleCount = StaticMesh->GetNumTriangles(0);
		R.VertexCount = StaticMesh->GetNumVertices(0);
		const FBox Box = StaticMesh->GetBoundingBox();
		R.BoundsMin = Box.Min;
		R.BoundsMax = Box.Max;
		R.LODCount = StaticMesh->GetNumLODs();
		R.CollisionShapes = StaticMesh->GetBodySetup() ? StaticMesh->GetBodySetup()->AggGeom.GetElementCount() : 0;
		R.MaterialPaths.Reset();
		R.Textures.Reset();
		TSet<UTexture*> Seen;
		for (const FStaticMaterial& Slot : StaticMesh->GetStaticMaterials())
		{
			R.MaterialPaths.Add(Slot.MaterialInterface ? Slot.MaterialInterface->GetPathName() : FString(TEXT("None")));
			if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Slot.MaterialInterface))
			{
				for (const FTextureParameterValue& Param : Instance->TextureParameterValues)
				{
					UTexture* Texture = Param.ParameterValue;
					if (!Texture || Seen.Contains(Texture)) { continue; }
					Seen.Add(Texture);
					R.Textures.Add(FString::Printf(TEXT("%s %lldx%lld max %d"), *Texture->GetPathName(),
						Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Texture->MaxTextureSize));
				}
			}
		}
	}

	/**
	 * A free name in Folder for Desired. An existing asset of that name is deleted and its name reused only when an earlier
	 * intake made it (it carries the intake tag) and nothing references it any more; anything else is left alone and the
	 * new asset gets a numbered name.
	 */
	FString ClaimAssetName(const FString& Folder, const FString& Desired)
	{
		FString Name = Desired;
		for (int32 Suffix = 2; Suffix < 100; ++Suffix)
		{
			const FString Path = Folder / Name;
			if (!UEditorAssetLibrary::DoesAssetExist(Path)) { return Name; }
			UObject* Old = UEditorAssetLibrary::LoadAsset(Path);
			const bool bOurs = Old && !UEditorAssetLibrary::GetMetadataTag(Old, IntakeSourceTag).IsEmpty();
			if (bOurs && UEditorAssetLibrary::FindPackageReferencersForAsset(Path, true).Num() == 0 && UEditorAssetLibrary::DeleteAsset(Path)) { return Name; }
			Name = FString::Printf(TEXT("%s_%d"), *Desired, Suffix);
		}
		return Name;
	}

	FString SanitizeAssetName(const FString& In)
	{
		FString Out;
		for (const TCHAR C : In)
		{
			Out.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
		}
		return Out.IsEmpty() ? FString(TEXT("Tex")) : Out;
	}
}

FModelingIntakeReport UModelingService::IntakeGeneratedMesh(const FString& SourceFile, const FString& AssetPath, float TargetSizeCm, const FString& FitAxis,
	float YawDeg, const FString& Pivot, int32 MaxTriangles, int32 MaxTextureSize, const FString& Collision, int32 MaxConvexHulls, const FString& LODPercents,
	FVector TargetBoxCm, bool bEnableNanite, bool bForce, bool bSaveAsset)
{
	// ---- validate everything before touching the project
	FString File = SourceFile.TrimStartAndEnd().TrimQuotes();
	FPaths::NormalizeFilename(File);
	if (!FPaths::FileExists(File)) { return IntakeFail(AssetPath, FString::Printf(TEXT("Source file not found: %s"), *File)); }
	if (!AssetPath.StartsWith(TEXT("/")) || AssetPath.Contains(TEXT(".")) || FPackageName::GetLongPackagePath(AssetPath).IsEmpty())
	{
		return IntakeFail(AssetPath, TEXT("AssetPath must be a package path like /Game/Props/Lamp/SM_Lamp (materials and textures go next to it)"));
	}
	const FString Fit = FitAxis.TrimStartAndEnd();
	static const TArray<FString> Fits = { TEXT("Height"), TEXT("Width"), TEXT("Depth"), TEXT("Longest"), TEXT("Box"), TEXT("None") };
	if (!Fits.ContainsByPredicate([&Fit](const FString& F) { return F.Equals(Fit, ESearchCase::IgnoreCase); }))
	{
		return IntakeFail(AssetPath, FString::Printf(TEXT("Unknown FitAxis '%s' (use Height, Width, Depth, Longest, Box, None)"), *FitAxis));
	}
	const bool bFitBox = Fit.Equals(TEXT("Box"), ESearchCase::IgnoreCase);
	const bool bFitNone = Fit.Equals(TEXT("None"), ESearchCase::IgnoreCase);
	if (bFitBox && TargetBoxCm.GetMin() <= 0.0) { return IntakeFail(AssetPath, TEXT("FitAxis 'Box' needs TargetBoxCm with three positive sizes (X, Y, Z in cm)")); }
	if (!bFitBox && !bFitNone && TargetSizeCm <= 0.f) { return IntakeFail(AssetPath, TEXT("TargetSizeCm must be positive")); }
	const FString PivotMode = Pivot.TrimStartAndEnd();
	static const TArray<FString> Pivots = { TEXT("BottomCenter"), TEXT("Center"), TEXT("TopCenter"), TEXT("None") };
	if (!Pivots.ContainsByPredicate([&PivotMode](const FString& P) { return P.Equals(PivotMode, ESearchCase::IgnoreCase); }))
	{
		return IntakeFail(AssetPath, FString::Printf(TEXT("Unknown Pivot '%s' (use BottomCenter, Center, TopCenter, None)"), *Pivot));
	}
	FString CollisionMethod = Collision.TrimStartAndEnd();
	if (CollisionMethod.Equals(TEXT("Box"), ESearchCase::IgnoreCase)) { CollisionMethod = TEXT("AlignedBoxes"); }
	else if (CollisionMethod.Equals(TEXT("Hulls"), ESearchCase::IgnoreCase)) { CollisionMethod = TEXT("ConvexHulls"); }
	const bool bCollision = !CollisionMethod.Equals(TEXT("None"), ESearchCase::IgnoreCase) && !CollisionMethod.IsEmpty();
	EGeometryScriptCollisionGenerationMethod ParsedMethod;
	if (bCollision && !ParseEnum(CollisionMethod, ParsedMethod))
	{
		return IntakeFail(AssetPath, FString::Printf(TEXT("Unknown Collision '%s' (use None, Box, Hulls, %s)"), *Collision, *EnumNames<EGeometryScriptCollisionGenerationMethod>()));
	}
	TArray<float> LODs;
	{
		TArray<FString> Parts;
		LODPercents.ParseIntoArray(Parts, TEXT(","), true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			const float Value = FCString::Atof(*Part);
			if (!Part.IsNumeric() || Value <= 0.f || Value > 1.f) { return IntakeFail(AssetPath, FString::Printf(TEXT("LODPercents '%s': every entry must be a fraction in (0, 1], e.g. \"1,0.5,0.25\""), *LODPercents)); }
			LODs.Add(Value);
		}
	}

	UStaticMesh* Existing = nullptr;
	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		Existing = LoadAssetAs<UStaticMesh>(AssetPath);
		if (!Existing) { return IntakeFail(AssetPath, FString::Printf(TEXT("%s exists but is not a StaticMesh"), *AssetPath)); }
		// Only a mesh an earlier intake made is replaced without asking: anything else at this path is somebody's asset.
		if (!bForce && UEditorAssetLibrary::GetMetadataTag(Existing, IntakeSourceTag).IsEmpty())
		{
			return IntakeFail(AssetPath, FString::Printf(TEXT("%s already exists and was not made by intake_generated_mesh: choose another asset path, or pass force=True to replace its geometry, materials, LODs and collision"), *AssetPath));
		}
	}
	const FString Fingerprint = LexToString(FMD5Hash::HashFile(*File)) + FString::Printf(TEXT("|%g|%s|%g|%s|%d|%d|%s|%d|%s|%s|%d"),
		TargetSizeCm, *Fit, YawDeg, *PivotMode, MaxTriangles, MaxTextureSize, *CollisionMethod, MaxConvexHulls, *LODPercents, *TargetBoxCm.ToString(), bEnableNanite ? 1 : 0);
	if (Existing && !bForce && UEditorAssetLibrary::GetMetadataTag(Existing, IntakeFingerprintTag) == Fingerprint)
	{
		FModelingIntakeReport R;
		R.bSuccess = true;
		R.bUpToDate = true;
		R.AssetPath = AssetPath;
		DescribeIntakeAsset(Existing, R);
		R.Message = FString::Printf(TEXT("%s is up to date with %s and these settings (%d tris, %d LODs); nothing re-imported (pass force=True to redo it)"),
			*AssetPath, *FPaths::GetCleanFilename(File), R.TriangleCount, R.LODCount);
		return R;
	}

	const FString Folder = FPackageName::GetLongPackagePath(AssetPath);
	FString Base = FPackageName::GetShortName(AssetPath);
	Base.RemoveFromStart(TEXT("SM_"));
	const FString Stage = Folder / (TEXT("_Intake_") + Base);
	const auto DropStage = [&Stage]()
	{
		if (UEditorAssetLibrary::DoesDirectoryExist(Stage)) { UEditorAssetLibrary::DeleteDirectory(Stage); }
	};
	DropStage();

	// ---- 1. import into a staging folder
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = File;
	Task->DestinationPath = Stage;
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->bAsync = false;
	FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });
	FAssetCompilingManager::Get().FinishAllCompilation();

	TArray<UStaticMesh*> Meshes;
	TArray<UTexture*> StagedTextures;
	TSet<FString> OtherClasses;
	for (const FString& Path : UEditorAssetLibrary::ListAssets(Stage, true, false))
	{
		UObject* Object = UEditorAssetLibrary::LoadAsset(Path);
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object)) { Meshes.Add(StaticMesh); }
		else if (UTexture* Texture = Cast<UTexture>(Object)) { StagedTextures.Add(Texture); }
		else if (Object && !Object->IsA<UMaterialInterface>()) { OtherClasses.Add(Object->GetClass()->GetName()); }
	}
	if (Meshes.Num() == 0)
	{
		DropStage();
		return IntakeFail(AssetPath, FString::Printf(TEXT("Importing %s produced no StaticMesh%s (a skinned or animated file imports as a SkeletalMesh; intake is for static props)"),
			*File, OtherClasses.Num() ? *FString::Printf(TEXT(" (got %s)"), *FString::Join(OtherClasses.Array(), TEXT(", "))) : TEXT("")));
	}
	Meshes.Sort([](const UStaticMesh& A, const UStaticMesh& B) { return A.GetName() < B.GetName(); });

	// ---- 2. merge every mesh of the file into one session mesh with one material list
	FModelingIntakeReport R;
	R.AssetPath = AssetPath;
	R.SourceMeshCount = Meshes.Num();
	int32 Handle = -1;
	UDynamicMesh* Combined = NewSessionMesh(Handle);
	const auto Abort = [&](const FString& Why)
	{
		ReleaseMesh(Handle);
		DropStage();
		return IntakeFail(AssetPath, Why);
	};
	TArray<UMaterialInterface*> Materials;
	UGeometryScriptDebug* Debug = NewDebug();
	for (UStaticMesh* StaticMesh : Meshes)
	{
		UDynamicMesh* Part = NewScratchMesh();
		if (!CopyFromStaticMesh(StaticMesh, Part, 0, Debug)) { return Abort(FString::Printf(TEXT("Could not read imported %s: %s"), *StaticMesh->GetName(), *DebugText(Debug))); }
		R.SourceTriangles += TriCount(Part);
		TArray<UMaterialInterface*> PartMaterials;
		TArray<int32> MaterialIndices;
		TArray<FName> SlotNames;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::GetSectionMaterialListFromStaticMesh(StaticMesh, FGeometryScriptMeshReadLOD(), PartMaterials, MaterialIndices, SlotNames, Outcome, Debug);
		TArray<UMaterialInterface*> Merged;
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshWithMaterials(Combined, Materials, Part, PartMaterials, Merged, FTransform::Identity, false,
			FGeometryScriptAppendMeshOptions(), true, Debug);
		Materials = Merged;
	}
	if (TriCount(Combined) == 0) { return Abort(FString::Printf(TEXT("%s has no triangles"), *File)); }

	// ---- 3. orient, fit to real-world size, pivot
	if (!FMath::IsNearlyZero(YawDeg)) { RotateMesh(Handle, FRotator(0.f, YawDeg, 0.f), FVector::ZeroVector); }
	FBox Bounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(Combined);
	const FVector Size = Bounds.GetSize();
	R.SourceSize = Size;
	FVector Scale = FVector::OneVector;
	if (bFitBox)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Scale[Axis] = Size[Axis] > UE_KINDA_SMALL_NUMBER ? TargetBoxCm[Axis] / Size[Axis] : 1.0;   // a flat card keeps its zero thickness
		}
	}
	else if (!bFitNone)
	{
		const double Measured = Fit.Equals(TEXT("Height"), ESearchCase::IgnoreCase) ? Size.Z
			: Fit.Equals(TEXT("Width"), ESearchCase::IgnoreCase) ? Size.X
			: Fit.Equals(TEXT("Depth"), ESearchCase::IgnoreCase) ? Size.Y : Size.GetMax();
		if (Measured <= UE_KINDA_SMALL_NUMBER) { return Abort(FString::Printf(TEXT("The model has no extent along FitAxis '%s' (size %s cm)"), *Fit, *Size.ToCompactString())); }
		Scale = FVector(TargetSizeCm / Measured);
	}
	if (!Scale.Equals(FVector::OneVector, 1e-6)) { ScaleMesh(Handle, Scale, FVector::ZeroVector); }
	R.ScaleApplied = Scale;
	Bounds = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(Combined);
	const FVector Center = Bounds.GetCenter();
	FVector Shift = FVector::ZeroVector;
	if (PivotMode.Equals(TEXT("BottomCenter"), ESearchCase::IgnoreCase)) { Shift = FVector(-Center.X, -Center.Y, -Bounds.Min.Z); }
	else if (PivotMode.Equals(TEXT("TopCenter"), ESearchCase::IgnoreCase)) { Shift = FVector(-Center.X, -Center.Y, -Bounds.Max.Z); }
	else if (PivotMode.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) { Shift = -Center; }
	if (!Shift.IsNearlyZero()) { TranslateMesh(Handle, Shift); }

	// ---- 4. decimate
	if (MaxTriangles > 0 && TriCount(Combined) > MaxTriangles)
	{
		SimplifyToTriangleCount(Handle, MaxTriangles, true);
	}

	// ---- 5. write ONE static mesh (in place when it exists, so placed actors keep their reference)
	TArray<FName> SlotNames;
	for (int32 i = 0; i < Materials.Num(); ++i) { SlotNames.Add(*FString::Printf(TEXT("Material_%d"), i)); }
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UStaticMesh* Target = Existing;
	if (Existing)
	{
		FGeometryScriptCopyMeshToAssetOptions Options;
		Options.bReplaceMaterials = true;
		for (UMaterialInterface* Material : Materials) { Options.NewMaterials.Add(Material); }
		Options.NewMaterialSlotNames = SlotNames;
		Options.bApplyNaniteSettings = true;
		Options.NewNaniteSettings.bEnabled = bEnableNanite;
		FGeometryScriptMeshWriteLOD WriteLOD;
		WriteLOD.LODIndex = 0;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(Combined, Existing, Options, WriteLOD, Outcome, true, Debug);
	}
	else
	{
		FGeometryScriptCreateNewStaticMeshAssetOptions Options;
		Options.bEnableNanite = bEnableNanite;
		Options.bEnableCollision = bCollision;
		Target = UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewStaticMeshAssetFromMesh(Combined, AssetPath, Options, Outcome, Debug);
		if (Target)
		{
			TArray<FStaticMaterial> Slots;
			for (int32 i = 0; i < Materials.Num(); ++i) { Slots.Add(FStaticMaterial(Materials[i], SlotNames[i])); }
			Target->SetStaticMaterials(Slots);
			Target->PostEditChange();
		}
	}
	if (Outcome != EGeometryScriptOutcomePins::Success || !Target) { return Abort(FString::Printf(TEXT("Writing %s failed: %s"), *AssetPath, *DebugText(Debug))); }
	ReleaseMesh(Handle);
	// Marked as intake's from here on, so a run that fails in a later step can simply be repeated.
	UEditorAssetLibrary::SetMetadataTag(Target, IntakeSourceTag, File);

	// ---- 6. keep the materials and textures (next to the mesh, stable names); drop the rest of the import
	TArray<UObject*> ToSave = { Target };
	TArray<FString> MovedFrom;
	const FString StagePrefix = Stage + TEXT("/");
	const auto Move = [&](UObject* Object, const FString& DesiredName)
	{
		const FString OldPackage = Object->GetOutermost()->GetName();
		const FString NewName = ClaimAssetName(Folder, DesiredName);
		if (UEditorAssetLibrary::RenameAsset(OldPackage, Folder / NewName))
		{
			MovedFrom.Add(OldPackage + TEXT(".") + Object->GetName());
			UEditorAssetLibrary::SetMetadataTag(Object, IntakeSourceTag, File);   // lets a later intake recognise (and replace) its own leftovers
			ToSave.Add(Object);
		}
	};
	TMap<UTexture*, FString> TextureNames;
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Materials[i]))
		{
			for (const FTextureParameterValue& Param : Instance->TextureParameterValues)
			{
				if (Param.ParameterValue && !TextureNames.Contains(Param.ParameterValue))
				{
					FString Role = Param.ParameterInfo.Name.ToString();
					Role.RemoveFromEnd(TEXT("Texture"));
					Role.RemoveFromEnd(TEXT("Map"));
					TextureNames.Add(Param.ParameterValue, FString::Printf(TEXT("T_%s_%s%s"), *Base, *SanitizeAssetName(Role),
						Materials.Num() > 1 ? *FString::Printf(TEXT("_%d"), i) : TEXT("")));
				}
			}
		}
	}
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		if (Materials[i] && Materials[i]->GetPathName().StartsWith(StagePrefix))
		{
			Move(Materials[i], FString::Printf(TEXT("%s_%s_%d"), Materials[i]->IsA<UMaterialInstance>() ? TEXT("MI") : TEXT("M"), *Base, i));
		}
	}
	for (UTexture* Texture : StagedTextures)   // every texture of the import: a plain UMaterial can reference them without parameters
	{
		const FString* Named = TextureNames.Find(Texture);
		Move(Texture, Named ? *Named : FString::Printf(TEXT("T_%s_%s"), *Base, *SanitizeAssetName(Texture->GetName())));
		if (MaxTextureSize > 0 && FMath::Max(Texture->Source.GetSizeX(), Texture->Source.GetSizeY()) > MaxTextureSize)
		{
			Texture->Modify();
			Texture->MaxTextureSize = MaxTextureSize;
			Texture->PostEditChange();
		}
	}
	TArray<UObjectRedirector*> Redirectors;
	for (const FString& Old : MovedFrom)
	{
		if (UObjectRedirector* Redirector = FindObject<UObjectRedirector>(nullptr, *Old)) { Redirectors.Add(Redirector); }
	}
	if (Redirectors.Num()) { FAssetToolsModule::GetModule().Get().FixupReferencers(Redirectors, false); }
	DropStage();

	// ---- 7. LODs, collision, provenance
	if (LODs.Num() > 0 || Target->GetNumSourceModels() > 1)
	{
		const FModelingResult LODResult = SetLODs(AssetPath, LODs.Num() ? LODs : TArray<float>{ 1.f }, true, false);
		if (!LODResult.bSuccess) { return IntakeFail(AssetPath, LODResult.Message); }
	}
	if (bCollision)
	{
		const FModelingResult CollisionResult = GenerateCollision(AssetPath, CollisionMethod, MaxConvexHulls, 25, false);
		if (!CollisionResult.bSuccess) { return IntakeFail(AssetPath, CollisionResult.Message); }
	}
	else if (UBodySetup* Body = Target->GetBodySetup())
	{
		Body->Modify();
		Body->RemoveSimpleCollision();
	}
	UEditorAssetLibrary::SetMetadataTag(Target, IntakeSourceTag, File);
	UEditorAssetLibrary::SetMetadataTag(Target, IntakeFingerprintTag, Fingerprint);
	Target->MarkPackageDirty();
	if (bSaveAsset) { UEditorAssetLibrary::SaveLoadedAssets(ToSave, false); }

	// ---- 8. report
	R.bSuccess = true;
	DescribeIntakeAsset(Target, R);
	const double Aniso = Scale.GetMax() / FMath::Max(Scale.GetMin(), UE_SMALL_NUMBER) - 1.0;
	R.Message = FString::Printf(TEXT("%s %s from %s: %d mesh(es), %d -> %d tris, size %s cm (imported %s, scale %s%s), %d LOD(s), %d collision shape(s), %d material slot(s), %d texture(s)%s"),
		Existing ? TEXT("Replaced") : TEXT("Created"), *AssetPath, *FPaths::GetCleanFilename(File), R.SourceMeshCount, R.SourceTriangles, R.TriangleCount,
		*(R.BoundsMax - R.BoundsMin).ToCompactString(), *Size.ToCompactString(), *Scale.ToCompactString(),
		Aniso > 0.05 ? *FString::Printf(TEXT(", stretched %.0f%%: check the proportions"), Aniso * 100.0) : TEXT(""),
		R.LODCount, R.CollisionShapes, R.MaterialPaths.Num(), R.Textures.Num(),
		MaxTextureSize > 0 ? *FString::Printf(TEXT(" capped at %d"), MaxTextureSize) : TEXT(""));
	return R;
}

namespace
{
	/** Shared tail of every bake: run the baker, then save one Texture2D asset per requested type (T_<BaseName>_<Suffix>). */
	FModelingBakeResult RunBake(UDynamicMesh* Target, UDynamicMesh* Source, const TArray<FGeometryScriptBakeTypeOptions>& Types, const TArray<FString>& Suffixes,
		int32 Resolution, const FString& OutputFolder, const FString& BaseName, int32 TargetUVLayer, int32 SamplesPerPixel)
	{
		FModelingBakeResult Result;
		FGeometryScriptBakeTargetMeshOptions TargetOptions;
		TargetOptions.TargetUVLayer = TargetUVLayer;
		FGeometryScriptBakeSourceMeshOptions SourceOptions;
		FGeometryScriptBakeTextureOptions BakeOptions;
		if (!ParseEnum(FString::Printf(TEXT("Resolution%d"), Resolution), BakeOptions.Resolution))
		{
			Result.Message = FString::Printf(TEXT("Unsupported resolution %d (use a power of two from 16 to 8192)"), Resolution);
			return Result;
		}
		if (!ParseEnum(FString::Printf(TEXT("Sample%d"), SamplesPerPixel), BakeOptions.SamplesPerPixel))
		{
			BakeOptions.SamplesPerPixel = EGeometryScriptBakeSamplesPerPixel::Sample1;
		}
		UGeometryScriptDebug* Debug = NewDebug();
		// The baker samples through the target's tangent frame; session meshes rarely carry tangents yet.
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(Target, FGeometryScriptTangentsOptions(), Debug);
		const TArray<UTexture2D*> Textures = UGeometryScriptLibrary_MeshBakeFunctions::BakeTexture(Target, FTransform::Identity, TargetOptions,
			Source, FTransform::Identity, SourceOptions, Types, BakeOptions, Debug);
		if (DebugHasErrors(Debug) || Textures.Num() != Types.Num())
		{
			Result.Message = FString::Printf(TEXT("Bake failed: %s"), *DebugText(Debug));
			return Result;
		}
		for (int32 i = 0; i < Textures.Num(); ++i)
		{
			if (!Textures[i]) { continue; }
			FGeometryScriptCreateNewTexture2DAssetOptions TextureOptions;
			TextureOptions.bOverwriteIfExists = true;
			EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
			const FString AssetPath = OutputFolder / FString::Printf(TEXT("T_%s_%s"), *BaseName, *Suffixes[i]);
			UTexture2D* Saved = UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewTexture2DAsset(Textures[i], AssetPath, TextureOptions, Outcome, Debug);
			if (Outcome != EGeometryScriptOutcomePins::Success || !Saved)
			{
				Result.Message = FString::Printf(TEXT("Could not save %s: %s"), *AssetPath, *DebugText(Debug));
				return Result;
			}
			Saved->BlockOnAnyAsyncBuild();   // the texture compiles asynchronously; finish it so a save or delete right after cannot race it
			UEditorAssetLibrary::SaveAsset(Saved->GetPathName(), false);
			Result.TexturePaths.Add(Saved->GetPathName());
		}
		Result.bSuccess = true;
		Result.Message = FString::Printf(TEXT("Baked %d texture(s) at %d"), Result.TexturePaths.Num(), Resolution);
		return Result;
	}
}

FModelingBakeResult UModelingService::BakeTextures(int32 TargetHandle, int32 SourceHandle, const FString& BakeTypes, int32 Resolution, const FString& OutputFolder,
	const FString& BaseName, int32 TargetUVLayer, int32 SamplesPerPixel)
{
	FModelingBakeResult Result;
	UDynamicMesh* Target = FindMesh(TargetHandle);
	if (!Target) { Result.Message = NoMesh(TargetHandle).Message; return Result; }
	UDynamicMesh* Source = FindMesh(SourceHandle);
	if (!Source) { Result.Message = NoMesh(SourceHandle).Message; return Result; }
	if (!OutputFolder.StartsWith(TEXT("/")) || BaseName.IsEmpty()) { Result.Message = TEXT("OutputFolder must be a content path (e.g. /Game/Textures) and BaseName non-empty"); return Result; }

	TArray<FString> TypeNames;
	BakeTypes.ParseIntoArray(TypeNames, TEXT(","), true);
	TArray<FGeometryScriptBakeTypeOptions> Types;
	TArray<FString> Suffixes;
	for (FString Name : TypeNames)
	{
		Name.TrimStartAndEndInline();
		if (Name.Equals(TEXT("TangentNormal"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeTangentNormal()); Suffixes.Add(TEXT("N")); }
		else if (Name.Equals(TEXT("ObjectNormal"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeObjectNormal()); Suffixes.Add(TEXT("ON")); }
		else if (Name.Equals(TEXT("FaceNormal"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeFaceNormal()); Suffixes.Add(TEXT("FN")); }
		else if (Name.Equals(TEXT("BentNormal"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeBentNormal()); Suffixes.Add(TEXT("BN")); }
		else if (Name.Equals(TEXT("AmbientOcclusion"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeAmbientOcclusion()); Suffixes.Add(TEXT("AO")); }
		else if (Name.Equals(TEXT("Curvature"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeCurvature()); Suffixes.Add(TEXT("Curv")); }
		else if (Name.Equals(TEXT("Position"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypePosition()); Suffixes.Add(TEXT("Pos")); }
		else if (Name.Equals(TEXT("VertexColor"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeVertexColor()); Suffixes.Add(TEXT("VC")); }
		else if (Name.Equals(TEXT("MaterialID"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeMaterialID()); Suffixes.Add(TEXT("ID")); }
		else if (Name.Equals(TEXT("UVShell"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeUVShell(TargetUVLayer, 1.f, FLinearColor::White, FLinearColor(0.25f, 0.25f, 0.25f), FLinearColor::Black)); Suffixes.Add(TEXT("UV")); }
		else if (Name.Equals(TEXT("Height"), ESearchCase::IgnoreCase)) { Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeHeight()); Suffixes.Add(TEXT("H")); }
		else
		{
			Result.Message = FString::Printf(TEXT("Unknown bake type '%s' (use TangentNormal, ObjectNormal, FaceNormal, BentNormal, AmbientOcclusion, Curvature, Position, VertexColor, MaterialID, UVShell, Height; textures via bake_texture_transfer)"), *Name);
			return Result;
		}
	}
	if (Types.Num() == 0) { Result.Message = TEXT("No bake types given"); return Result; }
	return RunBake(Target, Source, Types, Suffixes, Resolution, OutputFolder, BaseName, TargetUVLayer, SamplesPerPixel);
}

FModelingBakeResult UModelingService::BakeTextureTransfer(int32 TargetHandle, int32 SourceHandle, const FString& SourceTexturePaths, int32 Resolution, const FString& OutputFolder,
	const FString& BaseName, int32 TargetUVLayer, int32 SourceUVLayer, int32 SamplesPerPixel)
{
	FModelingBakeResult Result;
	UDynamicMesh* Target = FindMesh(TargetHandle);
	if (!Target) { Result.Message = NoMesh(TargetHandle).Message; return Result; }
	UDynamicMesh* Source = FindMesh(SourceHandle);
	if (!Source) { Result.Message = NoMesh(SourceHandle).Message; return Result; }
	if (!OutputFolder.StartsWith(TEXT("/")) || BaseName.IsEmpty()) { Result.Message = TEXT("OutputFolder must be a content path (e.g. /Game/Textures) and BaseName non-empty"); return Result; }
	TArray<FString> Paths;
	SourceTexturePaths.ParseIntoArray(Paths, TEXT(","), true);
	TArray<UTexture2D*> Textures;
	for (FString& Path : Paths)
	{
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty()) { continue; }
		UTexture2D* Texture = LoadAssetAs<UTexture2D>(Path);
		if (!Texture) { Result.Message = FString::Printf(TEXT("Texture2D not found: %s"), *Path); return Result; }
		Textures.Add(Texture);
	}
	if (Textures.Num() == 0) { Result.Message = TEXT("SourceTexturePaths must list at least one Texture2D asset (comma-separated, in source material-ID order for several)"); return Result; }
	TArray<FGeometryScriptBakeTypeOptions> Types;
	TArray<FString> Suffixes;
	if (Textures.Num() == 1)
	{
		Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeTexture(Textures[0], SourceUVLayer));
		Suffixes.Add(TEXT("Tex"));
	}
	else
	{
		Types.Add(UGeometryScriptLibrary_MeshBakeFunctions::MakeBakeTypeMultiTexture(Textures, SourceUVLayer));
		Suffixes.Add(TEXT("MultiTex"));
	}
	return RunBake(Target, Source, Types, Suffixes, Resolution, OutputFolder, BaseName, TargetUVLayer, SamplesPerPixel);
}

// =========================================================================
// More primitives
// =========================================================================

FModelingResult UModelingService::AppendCurvedStairs(int32 Handle, FTransform Transform, float StepWidth, float StepHeight, float InnerRadius,
	float CurveAngle, int32 NumSteps, bool bFloating, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCurvedStairs(Mesh, PrimitiveOptions(MaterialID), Transform, StepWidth, StepHeight, InnerRadius,
		CurveAngle, NumSteps, bFloating, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended curved stairs"));
}

FModelingResult UModelingService::AppendSphereBox(int32 Handle, FTransform Transform, float Radius, int32 Steps, const FString& Origin, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	EGeometryScriptPrimitiveOriginMode OriginMode; FString Error;
	if (!ParseOrigin(Origin, OriginMode, Error)) { return Fail(Handle, Error); }
	UGeometryScriptDebug* Debug = NewDebug();
	const int32 S = FMath::Clamp(Steps, 1, 64);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereBox(Mesh, PrimitiveOptions(MaterialID), Transform, Radius, S, S, S, OriginMode, Debug);
	return Finish(Handle, Mesh, Debug, TEXT("Appended sphere box"));
}

FModelingResult UModelingService::AppendSweepPolyline(int32 Handle, FTransform Transform, const TArray<FVector2D>& ProfilePoints, const TArray<FTransform>& SweepPath,
	bool bLoop, float StartScale, float EndScale, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (ProfilePoints.Num() < 2) { return Fail(Handle, TEXT("ProfilePoints needs at least 2 points")); }
	if (SweepPath.Num() < 2) { return Fail(Handle, TEXT("SweepPath needs at least 2 transforms")); }
	UGeometryScriptDebug* Debug = NewDebug();
	const TArray<float> NoTexParams;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSweepPolyline(Mesh, PrimitiveOptions(MaterialID), Transform, ProfilePoints, SweepPath,
		NoTexParams, NoTexParams, bLoop, StartScale, EndScale, 0.f, 1.f, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Appended sweep (%d profile points along %d path frames)"), ProfilePoints.Num(), SweepPath.Num()));
}

// =========================================================================
// More simplification
// =========================================================================

FModelingResult UModelingService::SimplifyToVertexCount(int32 Handle, int32 VertexCount, bool bAllowSeamCollapse)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptSimplifyMeshOptions Options;
	Options.bAllowSeamCollapse = bAllowSeamCollapse;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToVertexCount(Mesh, FMath::Max(4, VertexCount), Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Simplified toward %d vertices"), VertexCount));
}

FModelingResult UModelingService::SimplifyPlanar(int32 Handle, float AngleThresholdDeg)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptPlanarSimplifyOptions Options;
	Options.AngleThreshold = AngleThresholdDeg;
	UGeometryScriptDebug* Debug = NewDebug();
	const int32 Before = TriCount(Mesh);
	UGeometryScriptLibrary_MeshSimplifyFunctions::ApplySimplifyToPlanar(Mesh, Options, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Planar simplify (%d -> %d triangles)"), Before, TriCount(Mesh)));
}

// =========================================================================
// Spatial queries and sampling
// =========================================================================

FModelingSurfacePoint UModelingService::RayCast(int32 Handle, FVector Origin, FVector Direction, float MaxDistance)
{
	FModelingSurfacePoint Result;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { Result.Message = NoMesh(Handle).Message; return Result; }
	if (Direction.IsNearlyZero()) { Result.Message = TEXT("Direction must be non-zero"); return Result; }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptDynamicMeshBVH BVH;
	UGeometryScriptLibrary_MeshSpatial::BuildBVHForMesh(Mesh, BVH, Debug);
	FGeometryScriptSpatialQueryOptions Options;
	Options.MaxDistance = MaxDistance;
	FGeometryScriptRayHitResult Hit;
	EGeometryScriptSearchOutcomePins Outcome = EGeometryScriptSearchOutcomePins::NotFound;
	UGeometryScriptLibrary_MeshSpatial::FindNearestRayIntersectionWithMesh(Mesh, BVH, Origin, Direction.GetSafeNormal(), Options, Hit, Outcome, Debug);
	Result.bFound = Outcome == EGeometryScriptSearchOutcomePins::Found && Hit.bHit;
	Result.Position = Hit.HitPosition;
	Result.TriangleID = Hit.HitTriangleID;
	Result.Distance = Hit.RayParameter;
	Result.BaryCoords = Hit.HitBaryCoords;
	Result.Message = Result.bFound ? TEXT("Hit") : (DebugText(Debug).IsEmpty() ? TEXT("No hit") : DebugText(Debug));
	return Result;
}

FModelingSurfacePoint UModelingService::NearestPoint(int32 Handle, FVector Point, float MaxDistance)
{
	FModelingSurfacePoint Result;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { Result.Message = NoMesh(Handle).Message; return Result; }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptDynamicMeshBVH BVH;
	UGeometryScriptLibrary_MeshSpatial::BuildBVHForMesh(Mesh, BVH, Debug);
	FGeometryScriptSpatialQueryOptions Options;
	Options.MaxDistance = MaxDistance;
	FGeometryScriptTrianglePoint Nearest;
	EGeometryScriptSearchOutcomePins Outcome = EGeometryScriptSearchOutcomePins::NotFound;
	UGeometryScriptLibrary_MeshSpatial::FindNearestPointOnMesh(Mesh, BVH, Point, Options, Nearest, Outcome, Debug);
	Result.bFound = Outcome == EGeometryScriptSearchOutcomePins::Found && Nearest.bValid;
	Result.Position = Nearest.Position;
	Result.TriangleID = Nearest.TriangleID;
	Result.Distance = Result.bFound ? static_cast<float>(FVector::Distance(Point, Nearest.Position)) : 0.f;
	Result.BaryCoords = Nearest.BaryCoords;
	Result.Message = Result.bFound ? TEXT("Found") : (DebugText(Debug).IsEmpty() ? TEXT("Nothing within MaxDistance") : DebugText(Debug));
	return Result;
}

bool UModelingService::IsPointInside(int32 Handle, FVector Point)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return false; }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptDynamicMeshBVH BVH;
	UGeometryScriptLibrary_MeshSpatial::BuildBVHForMesh(Mesh, BVH, Debug);
	FGeometryScriptSpatialQueryOptions Options;
	bool bInside = false;
	EGeometryScriptContainmentOutcomePins Outcome = EGeometryScriptContainmentOutcomePins::Outside;
	UGeometryScriptLibrary_MeshSpatial::IsPointInsideMesh(Mesh, BVH, Point, Options, bInside, Outcome, Debug);
	return bInside;
}

TArray<FTransform> UModelingService::SampleSurfacePoints(int32 Handle, float SamplingRadius, int32 MaxSamples)
{
	TArray<FTransform> Samples;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return Samples; }
	FGeometryScriptMeshPointSamplingOptions Options;
	Options.SamplingRadius = FMath::Max(0.01f, SamplingRadius);
	Options.MaxNumSamples = FMath::Max(0, MaxSamples);
	FGeometryScriptIndexList TriangleIDs;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshSamplingFunctions::ComputePointSampling(Mesh, Options, Samples, TriangleIDs, Debug);
	return Samples;
}

bool UModelingService::SelectionBounds(int32 Handle, const FString& SelectionName, FVector& OutMin, FVector& OutMax)
{
	OutMin = OutMax = FVector::ZeroVector;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return false; }
	FGeometryScriptMeshSelection Selection; FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return false; }
	FBox Bounds(ForceInit);
	bool bEmpty = true;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshSelectionQueryFunctions::GetMeshSelectionBoundingBox(Mesh, Selection, Bounds, bEmpty, Debug);
	if (bEmpty) { return false; }
	OutMin = Bounds.Min;
	OutMax = Bounds.Max;
	return true;
}

int32 UModelingService::SelectByPolygroup(int32 Handle, const FString& SelectionName, int32 PolygroupID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::SelectMeshElementsByPolygroup(Mesh, FGeometryScriptGroupLayer(), PolygroupID, Selection, EGeometryScriptMeshSelectionType::Triangles);
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

// =========================================================================
// Hulls and comparison
// =========================================================================

namespace
{
	/**
	 * Booleans, deletes, and cuts leave gaps in the vertex/triangle id space. The containment
	 * algorithms index by id and assert on a non-compact mesh, so hull operations run on a
	 * compacted scratch copy of the source (the session mesh itself is left untouched).
	 */
	UDynamicMesh* CompactedCopy(UDynamicMesh* Source, UGeometryScriptDebug* Debug)
	{
		UDynamicMesh* Scratch = NewScratchMesh();
		UDynamicMesh* Out = nullptr;
		UGeometryScriptLibrary_MeshDecompositionFunctions::CopyMeshToMesh(Source, Scratch, Out, Debug);
		UGeometryScriptLibrary_MeshRepairFunctions::CompactMesh(Scratch, Debug);
		return Scratch;
	}
}

FModelingResult UModelingService::ConvexHull(int32 Handle, int32 SimplifyToFaceCount)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UDynamicMesh* Source = CompactedCopy(Mesh, Debug);
	int32 NewHandle = -1;
	UDynamicMesh* Hull = NewSessionMesh(NewHandle);
	UDynamicMesh* Out = nullptr;
	FGeometryScriptMeshSelection All;
	UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(Source, All, EGeometryScriptMeshSelectionType::Triangles);
	FGeometryScriptConvexHullOptions Options;
	Options.SimplifyToFaceCount = FMath::Max(0, SimplifyToFaceCount);
	UGeometryScriptLibrary_ContainmentFunctions::ComputeMeshConvexHull(Source, Hull, Out, All, Options, Debug);
	return Finish(NewHandle, Hull, Debug, FString::Printf(TEXT("Convex hull of handle %d"), Handle));
}

FModelingResult UModelingService::ConvexDecomposition(int32 Handle, int32 NumHulls)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UDynamicMesh* Source = CompactedCopy(Mesh, Debug);
	int32 NewHandle = -1;
	UDynamicMesh* Hulls = NewSessionMesh(NewHandle);
	UDynamicMesh* Out = nullptr;
	FGeometryScriptConvexDecompositionOptions Options;
	Options.NumHulls = FMath::Clamp(NumHulls, 1, 64);
	UGeometryScriptLibrary_ContainmentFunctions::ComputeMeshConvexDecomposition(Source, Hulls, Out, Options, Debug);
	return Finish(NewHandle, Hulls, Debug, FString::Printf(TEXT("Convex decomposition of handle %d into %d hulls"), Handle, NumHulls));
}

FModelingResult UModelingService::SweptHull(int32 Handle, FTransform ProjectionFrame)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UGeometryScriptDebug* Debug = NewDebug();
	UDynamicMesh* Source = CompactedCopy(Mesh, Debug);
	int32 NewHandle = -1;
	UDynamicMesh* Hull = NewSessionMesh(NewHandle);
	UDynamicMesh* Out = nullptr;
	FGeometryScriptSweptHullOptions Options;
	UGeometryScriptLibrary_ContainmentFunctions::ComputeMeshSweptHull(Source, Hull, Out, ProjectionFrame, Options, Debug);
	return Finish(NewHandle, Hull, Debug, FString::Printf(TEXT("Swept hull of handle %d"), Handle));
}

FModelingDistanceReport UModelingService::MeasureDistance(int32 HandleA, int32 HandleB)
{
	FModelingDistanceReport Report;
	UDynamicMesh* A = FindMesh(HandleA);
	if (!A) { Report.Message = NoMesh(HandleA).Message; return Report; }
	UDynamicMesh* B = FindMesh(HandleB);
	if (!B) { Report.Message = NoMesh(HandleB).Message; return Report; }
	FGeometryScriptMeasureMeshDistanceOptions Options;
	double MaxD = 0, MinD = 0, AvgD = 0, Rms = 0;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshComparisonFunctions::MeasureDistancesBetweenMeshes(A, B, Options, MaxD, MinD, AvgD, Rms, Debug);
	if (DebugHasErrors(Debug)) { Report.Message = DebugText(Debug); return Report; }
	Report.bSuccess = true;
	Report.MaxDistance = static_cast<float>(MaxD);
	Report.MinDistance = static_cast<float>(MinD);
	Report.AverageDistance = static_cast<float>(AvgD);
	Report.RootMeanSquareDeviation = static_cast<float>(Rms);
	Report.Message = FString::Printf(TEXT("max %.3f, mean %.3f, rms %.3f"), MaxD, AvgD, Rms);
	return Report;
}

// =========================================================================
// More skeletal
// =========================================================================

FModelingResult UModelingService::SmoothBoneWeights(int32 Handle, const FString& SkeletonPath, float Stiffness, int32 MaxInfluences)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	USkeleton* Skeleton = LoadAssetAs<USkeleton>(SkeletonPath);
	if (!Skeleton) { return Fail(Handle, FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath)); }
	FGeometryScriptSmoothBoneWeightsOptions Options;
	Options.Stiffness = Stiffness;
	Options.MaxInfluences = FMath::Clamp(MaxInfluences, 1, 12);
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBoneWeightFunctions::ComputeSmoothBoneWeights(Mesh, Skeleton, Options, FGeometryScriptBoneWeightProfile(), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Smoothed bone weights against %s"), *SkeletonPath));
}

FModelingResult UModelingService::PruneBoneWeights(int32 Handle, const FString& BoneNames)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	TArray<FString> Names;
	BoneNames.ParseIntoArray(Names, TEXT(","), true);
	TArray<FName> Bones;
	for (FString& Name : Names)
	{
		Name.TrimStartAndEndInline();
		if (!Name.IsEmpty()) { Bones.Add(FName(*Name)); }
	}
	if (Bones.Num() == 0) { return Fail(Handle, TEXT("BoneNames must list at least one bone (comma-separated)")); }
	FGeometryScriptPruneBoneWeightsOptions Options;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBoneWeightFunctions::PruneBoneWeights(Mesh, Bones, Options, FGeometryScriptBoneWeightProfile(), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Pruned %d bone(s) from skin weights"), Bones.Num()));
}

// =========================================================================
// Lofts, orientation, connected pieces
// =========================================================================

FModelingResult UModelingService::EnsureOutward(int32 Handle)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	float Area = 0.f, Volume = 0.f;
	UGeometryScriptLibrary_MeshQueryFunctions::GetMeshVolumeArea(Mesh, Area, Volume);
	if (Volume >= 0.f)
	{
		return Ok(Handle, Mesh, FString::Printf(TEXT("Already outward-facing (volume %.1f)"), Volume));
	}
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshNormalsFunctions::FlipNormals(Mesh, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Flipped an inward-facing mesh (volume was %.1f)"), Volume));
}

FModelingResult UModelingService::AppendLoft(int32 Handle, FTransform Transform, const TArray<FVector2D>& ProfilePoints, const TArray<FTransform>& Frames, int32 MaterialID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (ProfilePoints.Num() < 3) { return Fail(Handle, TEXT("ProfilePoints needs at least 3 points (a closed profile)")); }
	if (Frames.Num() < 2) { return Fail(Handle, TEXT("Frames needs at least 2 transforms")); }
	UGeometryScriptDebug* Debug = NewDebug();

	// Closed-polygon sweep with caps, built in scratch so the orientation fix can only touch the new part.
	// The generator maps profile X to each frame's Y axis and profile Y to its Z axis, scaled by the frame's Y/Z scale.
	UDynamicMesh* Part = NewScratchMesh();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSweepPolygon(Part, PrimitiveOptions(MaterialID), Transform, ProfilePoints, Frames,
		false, true, 1.f, 1.f, 0.f, 1.f, Debug);
	float Area = 0.f, Volume = 0.f;
	UGeometryScriptLibrary_MeshQueryFunctions::GetMeshVolumeArea(Part, Area, Volume);
	if (Volume < 0.f)
	{
		UGeometryScriptLibrary_MeshNormalsFunctions::FlipNormals(Part, Debug);
	}
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Part, FTransform::Identity, false, FGeometryScriptAppendMeshOptions(), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Appended loft (%d-point profile through %d frames%s)"),
		ProfilePoints.Num(), Frames.Num(), Volume < 0.f ? TEXT(", flipped outward") : TEXT("")));
}

int32 UModelingService::SelectConnected(int32 Handle, const FString& SelectionName, FVector Point)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptDynamicMeshBVH BVH;
	UGeometryScriptLibrary_MeshSpatial::BuildBVHForMesh(Mesh, BVH, Debug);
	FGeometryScriptTrianglePoint Nearest;
	EGeometryScriptSearchOutcomePins Outcome = EGeometryScriptSearchOutcomePins::NotFound;
	UGeometryScriptLibrary_MeshSpatial::FindNearestPointOnMesh(Mesh, BVH, Point, FGeometryScriptSpatialQueryOptions(), Nearest, Outcome, Debug);
	FGeometryScriptMeshSelection Selection;
	if (Outcome == EGeometryScriptSearchOutcomePins::Found && Nearest.bValid)
	{
		FGeometryScriptMeshSelection Seed;
		UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(Mesh, TArray<int32>{ Nearest.TriangleID }, EGeometryScriptMeshSelectionType::Triangles, Seed);
		UGeometryScriptLibrary_MeshSelectionFunctions::ExpandMeshSelectionToConnected(Mesh, Seed, Selection, EGeometryScriptTopologyConnectionType::Geometric);
	}
	StoreSelection(Handle, SelectionName, Selection);
	return Selection.GetNumSelected();
}

// =========================================================================
// Rigging: bones and skin weights authored on the mesh
// =========================================================================

namespace
{
	using FSkinWeights = FDynamicMeshVertexSkinWeightsAttribute;
	using UE::AnimationCore::FBoneWeight;
	using UE::AnimationCore::FBoneWeights;

	/** Mesh-space reference pose of every bone (poses are stored relative to the parent, parents precede children). */
	TArray<FTransform> BoneMeshTransforms(const FDynamicMeshAttributeSet* Attributes)
	{
		TArray<FTransform> Out;
		const int32 Num = Attributes->GetNumBones();
		Out.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Parent = Attributes->GetBoneParentIndices()->GetValue(i);
			const FTransform& Local = Attributes->GetBonePoses()->GetValue(i);
			Out[i] = (Parent >= 0 && Parent < i) ? Local * Out[Parent] : Local;
		}
		return Out;
	}

	int32 FindBoneIndexByName(const FDynamicMeshAttributeSet* Attributes, const FName Name)
	{
		for (int32 i = 0; i < Attributes->GetNumBones(); ++i)
		{
			if (Attributes->GetBoneNames()->GetValue(i) == Name) { return i; }
		}
		return INDEX_NONE;
	}

	FSkinWeights* DefaultSkinWeights(FDynamicMesh3& M)
	{
		return M.HasAttributes() ? M.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName) : nullptr;
	}
}

FModelingResult UModelingService::CreateBones(int32 Handle, const TArray<FModelingBoneDef>& Bones)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (Bones.Num() == 0) { return Fail(Handle, TEXT("Bones is empty: pass at least a root bone")); }

	TMap<FName, int32> IndexByName;
	TArray<FName> Names;
	TArray<int32> ParentIndices;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FName Name(*Bones[i].Name.TrimStartAndEnd());
		if (Name.IsNone()) { return Fail(Handle, FString::Printf(TEXT("Bone %d has no name"), i)); }
		if (IndexByName.Contains(Name)) { return Fail(Handle, FString::Printf(TEXT("Duplicate bone name '%s'"), *Name.ToString())); }
		const FString ParentName = Bones[i].ParentName.TrimStartAndEnd();
		int32 Parent = INDEX_NONE;
		if (i == 0)
		{
			if (!ParentName.IsEmpty() && ParentName != TEXT("None"))
			{
				return Fail(Handle, FString::Printf(TEXT("The first bone ('%s') is the root and cannot have a parent"), *Name.ToString()));
			}
		}
		else
		{
			const int32* Found = ParentName.IsEmpty() ? nullptr : IndexByName.Find(FName(*ParentName));
			if (!Found)
			{
				return Fail(Handle, FString::Printf(TEXT("Bone '%s' needs a parent listed before it (got '%s'); only the first bone is a root"), *Name.ToString(), *ParentName));
			}
			Parent = *Found;
		}
		IndexByName.Add(Name, i);
		Names.Add(Name);
		ParentIndices.Add(Parent);
	}

	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		M.EnableAttributes();
		FDynamicMeshAttributeSet* Attributes = M.Attributes();
		Attributes->DisableBones();
		Attributes->EnableBones(Bones.Num());
		TArray<FTransform> MeshSpace;
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			const FTransform& World = Bones[i].Transform;
			MeshSpace.Add(World);
			const int32 Parent = ParentIndices[i];
			Attributes->GetBoneNames()->SetValue(i, Names[i]);
			Attributes->GetBoneParentIndices()->SetValue(i, Parent);
			Attributes->GetBonePoses()->SetValue(i, Parent >= 0 ? World.GetRelativeTransform(MeshSpace[Parent]) : World);
		}
	});

	// A fresh default profile with everything on the root, so the mesh is a valid skeletal mesh immediately.
	bool bProfileExisted = false;
	UGeometryScriptLibrary_MeshBoneWeightFunctions::MeshCreateBoneWeights(Mesh, bProfileExisted, true, FGeometryScriptBoneWeightProfile());
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshBoneWeightFunctions::SetAllVertexBoneWeights(Mesh, TArray<FGeometryScriptBoneWeight>{ FGeometryScriptBoneWeight(0, 1.f) }, FGeometryScriptBoneWeightProfile(), Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Created %d bone(s) rooted at '%s'; every vertex bound to the root (bind_selection_to_bone to assign pieces)"), Bones.Num(), *Names[0].ToString()));
}

FModelingResult UModelingService::BindSelectionToBone(int32 Handle, const FString& SelectionName, const FString& BoneName, float Weight)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection;
	FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }

	int32 NumBones = 0;
	int32 BoneIndex = INDEX_NONE;
	const FName Bone(*BoneName.TrimStartAndEnd());
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		if (M.HasAttributes() && M.Attributes()->HasBones())
		{
			NumBones = M.Attributes()->GetNumBones();
			BoneIndex = FindBoneIndexByName(M.Attributes(), Bone);
		}
	});
	if (NumBones == 0) { return Fail(Handle, TEXT("Mesh has no bones: run create_bones first (or load a skeletal mesh)")); }
	if (BoneIndex == INDEX_NONE) { return Fail(Handle, FString::Printf(TEXT("No bone named '%s' (see list_bones)"), *BoneName)); }

	bool bProfileExisted = false;
	UGeometryScriptLibrary_MeshBoneWeightFunctions::MeshCreateBoneWeights(Mesh, bProfileExisted, false, FGeometryScriptBoneWeightProfile());

	FGeometryScriptIndexList Vertices;
	EGeometryScriptIndexType ResultType = EGeometryScriptIndexType::Any;
	UGeometryScriptLibrary_MeshSelectionFunctions::ConvertMeshSelectionToIndexList(Mesh, Selection, Vertices, ResultType, EGeometryScriptIndexType::Vertex);
	if (!Vertices.List.IsValid() || ResultType != EGeometryScriptIndexType::Vertex)
	{
		return Fail(Handle, FString::Printf(TEXT("Selection '%s' could not be converted to vertices"), *SelectionName));
	}

	const float W = FMath::Clamp(Weight, 0.f, 1.f);
	int32 Count = 0;
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		FSkinWeights* Skin = DefaultSkinWeights(M);
		if (!Skin) { return; }
		for (const int32 Vid : *Vertices.List)
		{
			if (!M.IsVertex(Vid)) { continue; }
			TArray<FBoneWeight> NewWeights;
			if (W < 1.f)
			{
				FBoneWeights Existing;
				Skin->GetValue(Vid, Existing);
				for (int32 k = 0; k < Existing.Num(); ++k)
				{
					const FBoneWeight& Old = Existing[k];
					if (Old.GetBoneIndex() != BoneIndex && Old.GetWeight() > 0.f)
					{
						NewWeights.Add(FBoneWeight(Old.GetBoneIndex(), Old.GetWeight() * (1.f - W)));
					}
				}
			}
			NewWeights.Add(FBoneWeight(static_cast<FBoneIndexType>(BoneIndex), W));
			Skin->SetValue(Vid, FBoneWeights::Create(NewWeights));
			++Count;
		}
	});
	return Ok(Handle, Mesh, FString::Printf(TEXT("Bound %d vertices to '%s' (weight %.2f)"), Count, *Bone.ToString(), W));
}

TArray<FModelingBoneInfo> UModelingService::ListBones(int32 Handle)
{
	TArray<FModelingBoneInfo> Out;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return Out; }
	Mesh->ProcessMesh([&Out](const FDynamicMesh3& M)
	{
		if (!M.HasAttributes() || !M.Attributes()->HasBones()) { return; }
		const FDynamicMeshAttributeSet* Attributes = M.Attributes();
		const TArray<FTransform> MeshSpace = BoneMeshTransforms(Attributes);
		Out.SetNum(Attributes->GetNumBones());
		for (int32 i = 0; i < Out.Num(); ++i)
		{
			const int32 Parent = Attributes->GetBoneParentIndices()->GetValue(i);
			Out[i].Index = i;
			Out[i].Name = Attributes->GetBoneNames()->GetValue(i).ToString();
			Out[i].ParentName = (Parent >= 0 && Parent < Out.Num()) ? Attributes->GetBoneNames()->GetValue(Parent).ToString() : FString();
			Out[i].LocalTransform = Attributes->GetBonePoses()->GetValue(i);
			Out[i].MeshTransform = MeshSpace[i];
		}
		if (const FSkinWeights* Skin = Attributes->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName))
		{
			FBoneWeights Weights;
			for (const int32 Vid : M.VertexIndicesItr())
			{
				Skin->GetValue(Vid, Weights);
				for (int32 k = 0; k < Weights.Num(); ++k)
				{
					const int32 BoneIndex = Weights[k].GetBoneIndex();
					if (Weights[k].GetWeight() > 0.f && BoneIndex < Out.Num()) { ++Out[BoneIndex].InfluencedVertices; }
				}
			}
		}
	});
	return Out;
}

namespace
{
	/** Comma-separated bone names -> attribute bone indices. Returns false and fills Error when any name is missing or the list is empty. */
	bool ParseBoneIndices(const FDynamicMeshAttributeSet* Attributes, const FString& BoneNames, TSet<int32>& OutIndices, FString& Error)
	{
		TArray<FString> Names;
		BoneNames.ParseIntoArray(Names, TEXT(","), true);
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty()) { continue; }
			const int32 Index = FindBoneIndexByName(Attributes, FName(*Name));
			if (Index == INDEX_NONE)
			{
				Error = FString::Printf(TEXT("No bone named '%s' (see list_bones)"), *Name);
				return false;
			}
			OutIndices.Add(Index);
		}
		if (OutIndices.Num() == 0)
		{
			Error = TEXT("BoneNames must list at least one bone (comma-separated)");
			return false;
		}
		return true;
	}

	/** Selection ("" = whole mesh) -> vertex id array. */
	bool SelectionVertices(int32 Handle, const FString& SelectionName, UDynamicMesh* Mesh, TArray<int32>& OutVertices, FString& Error)
	{
		FGeometryScriptMeshSelection Selection;
		if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return false; }
		FGeometryScriptIndexList Vertices;
		EGeometryScriptIndexType ResultType = EGeometryScriptIndexType::Any;
		UGeometryScriptLibrary_MeshSelectionFunctions::ConvertMeshSelectionToIndexList(Mesh, Selection, Vertices, ResultType, EGeometryScriptIndexType::Vertex);
		if (!Vertices.List.IsValid() || ResultType != EGeometryScriptIndexType::Vertex)
		{
			Error = FString::Printf(TEXT("Selection '%s' could not be converted to vertices"), *SelectionName);
			return false;
		}
		OutVertices = *Vertices.List;
		return true;
	}
}

int32 UModelingService::SelectByBoneWeight(int32 Handle, const FString& SelectionName, const FString& BoneNames, float MinWeight)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || SelectionName.IsEmpty()) { return -1; }
	const float Threshold = FMath::Clamp(MinWeight, 0.f, 1.f);
	TArray<int32> Vids;
	FString Error;
	bool bOk = false;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		if (!M.HasAttributes() || !M.Attributes()->HasBones()) { Error = TEXT("Mesh has no bones: run create_bones or load a skeletal mesh first"); return; }
		TSet<int32> Targets;
		if (!ParseBoneIndices(M.Attributes(), BoneNames, Targets, Error)) { return; }
		const FSkinWeights* Skin = M.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName);
		if (!Skin) { Error = TEXT("Mesh has no skin weights: transfer_bone_weights / bind_selection_to_bone first"); return; }
		FBoneWeights Weights;
		for (const int32 Vid : M.VertexIndicesItr())
		{
			Skin->GetValue(Vid, Weights);
			float Sum = 0.f;
			for (int32 k = 0; k < Weights.Num(); ++k)
			{
				if (Targets.Contains(static_cast<int32>(Weights[k].GetBoneIndex()))) { Sum += Weights[k].GetWeight(); }
			}
			if (Sum > 0.f && Sum >= Threshold) { Vids.Add(Vid); }
		}
		bOk = true;
	});
	if (!bOk)
	{
		UE_LOG(LogVibe3DModeling, Warning, TEXT("SelectByBoneWeight: %s"), *Error);
		return -1;
	}
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(Mesh, Vids, EGeometryScriptMeshSelectionType::Vertices, Selection);
	StoreSelection(Handle, SelectionName, Selection);
	return Vids.Num();
}

FModelingResult UModelingService::ScaleBoneWeights(int32 Handle, const FString& SelectionName, const FString& BoneNames, float Scale, const FString& ReceiverBoneName)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	TArray<int32> Vids;
	FString Error;
	if (!SelectionVertices(Handle, SelectionName, Mesh, Vids, Error)) { return Fail(Handle, Error); }

	const float S = FMath::Clamp(Scale, 0.f, 1.f);
	const FString Receiver = ReceiverBoneName.TrimStartAndEnd();
	int32 Count = 0;
	int32 SkippedFullyBound = 0;
	bool bOk = false;
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		if (!M.HasAttributes() || !M.Attributes()->HasBones()) { Error = TEXT("Mesh has no bones: run create_bones or load a skeletal mesh first"); return; }
		TSet<int32> Targets;
		if (!ParseBoneIndices(M.Attributes(), BoneNames, Targets, Error)) { return; }
		int32 ReceiverIndex = INDEX_NONE;
		if (!Receiver.IsEmpty())
		{
			ReceiverIndex = FindBoneIndexByName(M.Attributes(), FName(*Receiver));
			if (ReceiverIndex == INDEX_NONE) { Error = FString::Printf(TEXT("No bone named '%s' (see list_bones)"), *Receiver); return; }
			if (Targets.Contains(ReceiverIndex)) { Error = FString::Printf(TEXT("Receiver '%s' is also in BoneNames; the freed weight must go to a different bone"), *Receiver); return; }
		}
		FSkinWeights* Skin = DefaultSkinWeights(M);
		if (!Skin) { Error = TEXT("Mesh has no skin weights: transfer_bone_weights / bind_selection_to_bone first"); return; }

		FBoneWeights Existing;
		for (const int32 Vid : Vids)
		{
			if (!M.IsVertex(Vid)) { continue; }
			Skin->GetValue(Vid, Existing);
			TMap<int32, float> W;
			float TargetSum = 0.f;
			for (int32 k = 0; k < Existing.Num(); ++k)
			{
				const int32 BoneIndex = static_cast<int32>(Existing[k].GetBoneIndex());
				const float Weight = Existing[k].GetWeight();
				if (Weight <= 0.f) { continue; }
				W.FindOrAdd(BoneIndex) += Weight;
				if (Targets.Contains(BoneIndex)) { TargetSum += Weight; }
			}
			if (TargetSum <= 0.f) { continue; }                 // vertex untouched by the listed bones

			const float Freed = TargetSum * (1.f - S);
			for (const int32 BoneIndex : Targets)
			{
				if (float* Found = W.Find(BoneIndex)) { *Found *= S; }
			}

			int32 Into = ReceiverIndex;
			if (Into == INDEX_NONE)
			{
				float Best = 0.f;                                // strongest influence outside the target set
				for (const TPair<int32, float>& Pair : W)
				{
					if (!Targets.Contains(Pair.Key) && Pair.Value > Best) { Best = Pair.Value; Into = Pair.Key; }
				}
			}
			if (Into == INDEX_NONE) { ++SkippedFullyBound; continue; }  // nothing else owns this vertex; renormalizing would undo the scale

			W.FindOrAdd(Into) += Freed;
			TArray<FBoneWeight> NewWeights;
			for (const TPair<int32, float>& Pair : W)
			{
				if (Pair.Value > 0.f) { NewWeights.Add(FBoneWeight(static_cast<FBoneIndexType>(Pair.Key), Pair.Value)); }
			}
			Skin->SetValue(Vid, FBoneWeights::Create(NewWeights));
			++Count;
		}
		bOk = true;
	});
	if (!bOk) { return Fail(Handle, Error); }
	FString Skipped = SkippedFullyBound > 0 ? FString::Printf(TEXT("; %d vertices bound only to those bones skipped (name a ReceiverBoneName to move them)"), SkippedFullyBound) : FString();
	return Ok(Handle, Mesh, FString::Printf(TEXT("Scaled [%s] to %.2f on %d vertices, freed weight -> %s%s"),
		*BoneNames, S, Count, Receiver.IsEmpty() ? TEXT("strongest other influence") : *Receiver, *Skipped));
}

FModelingResult UModelingService::SmoothSelectionBoneWeights(int32 Handle, const FString& SelectionName, int32 Iterations, float Alpha)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	TArray<int32> Vids;
	FString Error;
	if (!SelectionVertices(Handle, SelectionName, Mesh, Vids, Error)) { return Fail(Handle, Error); }
	const int32 Iters = FMath::Clamp(Iterations, 1, 50);
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);

	int32 Count = 0;
	bool bOk = false;
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		FSkinWeights* Skin = DefaultSkinWeights(M);
		if (!Skin) { Error = TEXT("Mesh has no skin weights: transfer_bone_weights / bind_selection_to_bone first"); return; }

		const auto ReadWeights = [Skin](int32 Vid, TMap<int32, float>& Out)
		{
			FBoneWeights Weights;
			Skin->GetValue(Vid, Weights);
			for (int32 k = 0; k < Weights.Num(); ++k)
			{
				if (Weights[k].GetWeight() > 0.f) { Out.FindOrAdd(static_cast<int32>(Weights[k].GetBoneIndex())) += Weights[k].GetWeight(); }
			}
		};

		for (int32 Iter = 0; Iter < Iters; ++Iter)
		{
			// Jacobi step: gather smoothed values from the current state, then apply, so edit order can't bias the result.
			TArray<TPair<int32, TMap<int32, float>>> Pending;
			Pending.Reserve(Vids.Num());
			for (const int32 Vid : Vids)
			{
				if (!M.IsVertex(Vid)) { continue; }
				TMap<int32, float> Avg;
				int32 NumNeighbours = 0;
				for (const int32 Nbr : M.VtxVerticesItr(Vid))
				{
					ReadWeights(Nbr, Avg);
					++NumNeighbours;
				}
				if (NumNeighbours == 0) { continue; }
				for (TPair<int32, float>& Pair : Avg) { Pair.Value /= static_cast<float>(NumNeighbours); }

				TMap<int32, float> Own;
				ReadWeights(Vid, Own);
				TMap<int32, float> Blended;
				for (const TPair<int32, float>& Pair : Own) { Blended.FindOrAdd(Pair.Key) += Pair.Value * (1.f - A); }
				for (const TPair<int32, float>& Pair : Avg) { Blended.FindOrAdd(Pair.Key) += Pair.Value * A; }
				Pending.Emplace(Vid, MoveTemp(Blended));
			}
			for (TPair<int32, TMap<int32, float>>& Entry : Pending)
			{
				TArray<FBoneWeight> NewWeights;
				for (const TPair<int32, float>& Pair : Entry.Value)
				{
					if (Pair.Value > 0.005f) { NewWeights.Add(FBoneWeight(static_cast<FBoneIndexType>(Pair.Key), Pair.Value)); }  // drop dust so influences don't creep
				}
				if (NewWeights.Num() > 0)
				{
					Skin->SetValue(Entry.Key, FBoneWeights::Create(NewWeights));
					if (Iter == 0) { ++Count; }
				}
			}
		}
		bOk = true;
	});
	if (!bOk) { return Fail(Handle, Error); }
	return Ok(Handle, Mesh, FString::Printf(TEXT("Smoothed skin weights on %d vertices (%d iteration(s), alpha %.2f)"), Count, Iters, A));
}

FModelingResult UModelingService::InspectBoneWeights(int32 Handle, const FString& SelectionName)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	TArray<int32> Vids;
	FString Error;
	if (!SelectionVertices(Handle, SelectionName, Mesh, Vids, Error)) { return Fail(Handle, Error); }

	struct FBoneStat { int32 Vertices = 0; float SumWeight = 0.f; float MaxWeight = 0.f; };
	TMap<int32, FBoneStat> Stats;
	float MinSum = TNumericLimits<float>::Max();
	float MaxSum = 0.f;
	int32 MaxInfluences = 0;
	int32 OffNormalized = 0;
	int32 Counted = 0;
	TArray<FName> BoneNames;
	bool bOk = false;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		const FSkinWeights* Skin = M.HasAttributes() ? M.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName) : nullptr;
		if (!Skin) { Error = TEXT("Mesh has no skin weights: transfer_bone_weights / bind_selection_to_bone first"); return; }
		if (M.Attributes()->HasBones())
		{
			for (int32 i = 0; i < M.Attributes()->GetNumBones(); ++i) { BoneNames.Add(M.Attributes()->GetBoneNames()->GetValue(i)); }
		}
		FBoneWeights Weights;
		for (const int32 Vid : Vids)
		{
			if (!M.IsVertex(Vid)) { continue; }
			Skin->GetValue(Vid, Weights);
			float Sum = 0.f;
			int32 Influences = 0;
			for (int32 k = 0; k < Weights.Num(); ++k)
			{
				const float Weight = Weights[k].GetWeight();
				if (Weight <= 0.f) { continue; }
				FBoneStat& Stat = Stats.FindOrAdd(static_cast<int32>(Weights[k].GetBoneIndex()));
				++Stat.Vertices;
				Stat.SumWeight += Weight;
				Stat.MaxWeight = FMath::Max(Stat.MaxWeight, Weight);
				Sum += Weight;
				++Influences;
			}
			MinSum = FMath::Min(MinSum, Sum);
			MaxSum = FMath::Max(MaxSum, Sum);
			MaxInfluences = FMath::Max(MaxInfluences, Influences);
			if (FMath::Abs(Sum - 1.f) > 0.01f) { ++OffNormalized; }
			++Counted;
		}
		bOk = true;
	});
	if (!bOk) { return Fail(Handle, Error); }
	if (Counted == 0) { return Fail(Handle, FString::Printf(TEXT("Selection '%s' holds no vertices"), *SelectionName)); }

	Stats.ValueSort([](const FBoneStat& A, const FBoneStat& B) { return A.Vertices > B.Vertices; });
	FString Lines;
	int32 Shown = 0;
	for (const TPair<int32, FBoneStat>& Pair : Stats)
	{
		if (Shown++ >= 20) { Lines += FString::Printf(TEXT("\n  (+%d more bones)"), Stats.Num() - 20); break; }
		const FString Name = BoneNames.IsValidIndex(Pair.Key) ? BoneNames[Pair.Key].ToString() : FString::Printf(TEXT("bone#%d"), Pair.Key);
		Lines += FString::Printf(TEXT("\n  %s: %d verts, avg %.3f, max %.3f"), *Name, Pair.Value.Vertices, Pair.Value.SumWeight / Pair.Value.Vertices, Pair.Value.MaxWeight);
	}
	return Ok(Handle, Mesh, FString::Printf(TEXT("%d vertices, %d influencing bone(s), weight sums %.3f..%.3f, max influences %d, %d vertices off-normalized (>1%% from 1.0):%s"),
		Counted, Stats.Num(), MinSum, MaxSum, MaxInfluences, OffNormalized, *Lines));
}

FModelingResult UModelingService::SkinToSurface(int32 Handle, int32 SourceHandle, const FString& Mode, const FString& RigidBoneName,
	const FString& BonesFromSkeletalMesh, int32 SmoothIterations, float SmoothStrength)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UDynamicMesh* Source = FindMesh(SourceHandle);
	if (!Source) { return Fail(Handle, FString::Printf(TEXT("No source session mesh with handle %d"), SourceHandle)); }
	if (Source == Mesh) { return Fail(Handle, TEXT("SourceHandle must be a different mesh (the skinned surface underneath)")); }
	const FString UseMode = Mode.TrimStartAndEnd();
	const bool bRigid = UseMode.Equals(TEXT("Rigid"), ESearchCase::IgnoreCase);
	const bool bInpaint = UseMode.Equals(TEXT("Inpaint"), ESearchCase::IgnoreCase);
	if (!bRigid && !bInpaint && !UseMode.Equals(TEXT("Surface"), ESearchCase::IgnoreCase))
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown mode '%s' (use Surface, Inpaint, Rigid)"), *Mode));
	}
	bool bSourceSkinned = false;
	Source->ProcessMesh([&](const FDynamicMesh3& M)
	{
		bSourceSkinned = M.HasAttributes() && M.Attributes()->HasBones() && M.Attributes()->GetSkinWeightsAttribute(FSkeletalMeshAttributes::DefaultSkinWeightProfileName) != nullptr;
	});
	if (!bSourceSkinned) { return Fail(Handle, FString::Printf(TEXT("Source handle %d has no bones and weights: load it with load_mesh_from_skeletal_mesh / load_mesh_from_skinned_component"), SourceHandle)); }

	UGeometryScriptDebug* Debug = NewDebug();
	UDynamicMesh* BoneDonor = Source;
	FString BonesNote = FString::Printf(TEXT("bones of handle %d"), SourceHandle);
	if (!BonesFromSkeletalMesh.IsEmpty())
	{
		USkeletalMesh* Donor = LoadAssetAs<USkeletalMesh>(BonesFromSkeletalMesh);
		if (!Donor) { return Fail(Handle, FString::Printf(TEXT("SkeletalMesh not found: %s"), *BonesFromSkeletalMesh)); }
		BoneDonor = NewScratchMesh();
		if (!CopyFromSkeletalMesh(Donor, BoneDonor, 0, Debug)) { return Fail(Handle, FString::Printf(TEXT("Could not read %s: %s"), *BonesFromSkeletalMesh, *DebugText(Debug))); }
		BonesNote = FString::Printf(TEXT("bones of %s"), *BonesFromSkeletalMesh);
	}
	// The result's skeleton first, then weights matched to it by bone name.
	UGeometryScriptLibrary_MeshBoneWeightFunctions::CopyBonesFromMesh(BoneDonor, Mesh, FGeometryScriptCopyBonesFromMeshOptions(), Debug);
	FGeometryScriptTransferBoneWeightsOptions Options;
	Options.TransferMethod = bInpaint ? ETransferBoneWeightsMethod::InpaintWeights : ETransferBoneWeightsMethod::ClosestPointOnSurface;
	Options.OutputTargetMeshBones = EOutputTargetMeshBones::TargetBones;
	Options.NumSmoothingIterations = FMath::Clamp(SmoothIterations, 0, 100);
	Options.SmoothingStrength = FMath::Clamp(SmoothStrength, 0.f, 1.f);
	UGeometryScriptLibrary_MeshBoneWeightFunctions::TransferBoneWeightsFromMesh(Source, Mesh, Options, FGeometryScriptMeshSelection(), Debug);
	if (DebugHasErrors(Debug)) { return Fail(Handle, FString::Printf(TEXT("Weight transfer failed: %s"), *DebugText(Debug))); }

	// Share of the total weight per bone: the report, and the rigid mode's choice.
	TMap<int32, double> Share;
	double Total = 0.0;
	TArray<FName> BoneNames;
	int32 RigidIndex = INDEX_NONE;
	FString Error;
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		FSkinWeights* Skin = DefaultSkinWeights(M);
		if (!Skin || !M.Attributes()->HasBones()) { Error = TEXT("Transfer produced no skin weights"); return; }
		for (int32 i = 0; i < M.Attributes()->GetNumBones(); ++i) { BoneNames.Add(M.Attributes()->GetBoneNames()->GetValue(i)); }
		FBoneWeights Weights;
		for (const int32 Vid : M.VertexIndicesItr())
		{
			Skin->GetValue(Vid, Weights);
			for (int32 k = 0; k < Weights.Num(); ++k) { Share.FindOrAdd(Weights[k].GetBoneIndex()) += Weights[k].GetWeight(); Total += Weights[k].GetWeight(); }
		}
		if (!bRigid) { return; }
		if (!RigidBoneName.TrimStartAndEnd().IsEmpty())
		{
			RigidIndex = FindBoneIndexByName(M.Attributes(), FName(*RigidBoneName.TrimStartAndEnd()));
			if (RigidIndex == INDEX_NONE) { Error = FString::Printf(TEXT("No bone named '%s' in the %s (see list_bones)"), *RigidBoneName, *BonesNote); return; }
		}
		else
		{
			double Best = -1.0;
			for (const TPair<int32, double>& Pair : Share) { if (Pair.Value > Best) { Best = Pair.Value; RigidIndex = Pair.Key; } }
		}
		if (RigidIndex == INDEX_NONE) { Error = TEXT("No bone carries weight under the mesh"); return; }
		const FBoneWeights One = FBoneWeights::Create({ FBoneWeight(static_cast<FBoneIndexType>(RigidIndex), 1.f) });
		for (const int32 Vid : M.VertexIndicesItr()) { Skin->SetValue(Vid, One); }
	});
	if (!Error.IsEmpty()) { return Fail(Handle, Error); }

	Share.ValueSort([](double A, double B) { return A > B; });
	TArray<FString> Top;
	for (const TPair<int32, double>& Pair : Share)
	{
		if (Top.Num() >= 6 || Total <= 0.0) { break; }
		Top.Add(FString::Printf(TEXT("%s %.0f%%"), BoneNames.IsValidIndex(Pair.Key) ? *BoneNames[Pair.Key].ToString() : TEXT("?"), 100.0 * Pair.Value / Total));
	}
	return Ok(Handle, Mesh, bRigid
		? FString::Printf(TEXT("Rigid: every vertex on '%s' (surface under it: %s); %d %s"), *BoneNames[RigidIndex].ToString(), *FString::Join(Top, TEXT(", ")), BoneNames.Num(), *BonesNote)
		: FString::Printf(TEXT("%s weights from handle %d: %s; %d %s"), bInpaint ? TEXT("Inpainted") : TEXT("Closest-surface"), SourceHandle, *FString::Join(Top, TEXT(", ")), BoneNames.Num(), *BonesNote));
}

FModelingResult UModelingService::RemapLODBones(const FString& AssetPath, const FString& ReferenceSkeletalMeshPath, bool bSaveAsset)
{
	USkeletalMesh* SkeletalMesh = LoadAssetAs<USkeletalMesh>(AssetPath);
	if (!SkeletalMesh) { return Fail(-1, FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath)); }
	USkeletalMesh* Reference = LoadAssetAs<USkeletalMesh>(ReferenceSkeletalMeshPath);
	if (!Reference) { return Fail(-1, FString::Printf(TEXT("Reference SkeletalMesh not found: %s (pass the mesh this one follows, e.g. the body)"), *ReferenceSkeletalMeshPath)); }
	FAssetCompilingManager::Get().FinishAllCompilation();
	const FSkeletalMeshRenderData* RefRender = Reference->GetResourceForRendering();
	if (!RefRender || RefRender->LODRenderData.Num() == 0) { return Fail(-1, FString::Printf(TEXT("%s has no render data to read its LOD bone sets from"), *ReferenceSkeletalMeshPath)); }
	const int32 NumRefLODs = RefRender->LODRenderData.Num();
	const auto KeptAt = [&](int32 RefLOD)
	{
		TSet<FName> Names;
		for (const FBoneIndexType Bone : RefRender->LODRenderData[FMath::Clamp(RefLOD, 0, NumRefLODs - 1)].RequiredBones)
		{
			Names.Add(Reference->GetRefSkeleton().GetBoneName(Bone));
		}
		return Names;
	};

	TSet<FName> RefBones;
	for (int32 i = 0; i < Reference->GetRefSkeleton().GetRawBoneNum(); ++i) { RefBones.Add(Reference->GetRefSkeleton().GetBoneName(i)); }
	const int32 NumLODs = SkeletalMesh->GetLODNum();
	FString Lines;
	int32 TotalMoved = 0;
	for (int32 LOD = 0; LOD < NumLODs; ++LOD)
	{
		TSet<FName> Kept = KeptAt(LOD);
		for (int32 RefLOD = LOD + 1; LOD == NumLODs - 1 && RefLOD < NumRefLODs; ++RefLOD) { Kept = Kept.Intersect(KeptAt(RefLOD)); }

		const FModelingResult Loaded = LoadMeshFromSkeletalMesh(AssetPath, LOD);
		if (!Loaded.bSuccess) { return Fail(-1, Loaded.Message); }
		const int32 Handle = Loaded.Handle;
		UDynamicMesh* Mesh = FindMesh(Handle);
		TArray<FString> Remapped;
		int32 Moved = 0;
		Mesh->EditMesh([&](FDynamicMesh3& M)
		{
			FSkinWeights* Skin = DefaultSkinWeights(M);
			if (!Skin) { return; }
			// Bone index -> name / parent: the mesh's own bone list when it has one, else the asset's reference skeleton.
			TArray<FName> Names;
			TArray<int32> Parents;
			if (M.Attributes()->HasBones())
			{
				for (int32 i = 0; i < M.Attributes()->GetNumBones(); ++i)
				{
					Names.Add(M.Attributes()->GetBoneNames()->GetValue(i));
					Parents.Add(M.Attributes()->GetBoneParentIndices()->GetValue(i));
				}
			}
			else
			{
				const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
				for (int32 i = 0; i < Ref.GetRawBoneNum(); ++i) { Names.Add(Ref.GetBoneName(i)); Parents.Add(Ref.GetParentIndex(i)); }
			}
			TArray<int32> Target;
			Target.SetNum(Names.Num());
			for (int32 i = 0; i < Names.Num(); ++i)
			{
				// Only bones the reference HAS but drops at this LOD; bones of the follower's own (cloth, props) are left alone.
				Target[i] = i;
				if (!RefBones.Contains(Names[i]) || Kept.Contains(Names[i])) { continue; }
				int32 Walk = Parents.IsValidIndex(i) ? Parents[i] : INDEX_NONE;
				while (Walk != INDEX_NONE && !Kept.Contains(Names[Walk])) { Walk = Parents.IsValidIndex(Walk) ? Parents[Walk] : INDEX_NONE; }
				if (Walk != INDEX_NONE) { Target[i] = Walk; }
			}
			FBoneWeights Weights;
			for (const int32 Vid : M.VertexIndicesItr())
			{
				Skin->GetValue(Vid, Weights);
				bool bChanged = false;
				TMap<int32, float> Merged;
				for (int32 k = 0; k < Weights.Num(); ++k)
				{
					const int32 From = Weights[k].GetBoneIndex();
					const int32 To = Target.IsValidIndex(From) ? Target[From] : From;
					if (To != From && Weights[k].GetWeight() > 0.f)
					{
						bChanged = true;
						Remapped.AddUnique(FString::Printf(TEXT("%s->%s"), *Names[From].ToString(), *Names[To].ToString()));
					}
					Merged.FindOrAdd(To) += Weights[k].GetWeight();
				}
				if (!bChanged) { continue; }
				TArray<FBoneWeight> NewWeights;
				for (const TPair<int32, float>& Pair : Merged) { NewWeights.Add(FBoneWeight(static_cast<FBoneIndexType>(Pair.Key), Pair.Value)); }
				Skin->SetValue(Vid, FBoneWeights::Create(NewWeights));
				++Moved;
			}
		});
		if (Moved > 0)
		{
			FString WriteMessage;
			if (!WriteSkeletalLOD(Handle, Mesh, SkeletalMesh, LOD, WriteMessage))
			{
				ReleaseMesh(Handle);
				return Fail(-1, FString::Printf(TEXT("LOD %d: %s%s"), LOD, *WriteMessage,
					TotalMoved > 0 ? TEXT(" (the LODs before it were re-weighted in memory and NOT saved: fix this LOD and run again, or reload the asset to discard)") : TEXT("")));
			}
			Lines += FString::Printf(TEXT("\n  LOD %d: %d vertices moved off bones the reference drops (%s)"), LOD, Moved, *FString::Join(Remapped, TEXT(", ")));
		}
		else
		{
			Lines += FString::Printf(TEXT("\n  LOD %d: nothing weighted to a bone the reference drops"), LOD);
		}
		TotalMoved += Moved;
		ReleaseMesh(Handle);
	}
	if (TotalMoved > 0) { SaveIf(bSaveAsset, AssetPath); }
	FModelingResult R;
	R.bSuccess = true;
	R.AssetPath = AssetPath;
	R.VertexCount = TotalMoved;
	R.Message = FString::Printf(TEXT("%s against %s (%d LOD(s)): %d vertices re-weighted%s"), *AssetPath, *ReferenceSkeletalMeshPath, NumRefLODs, TotalMoved, *Lines);
	return R;
}

// =========================================================================
// Posed skinned components and mesh-vs-mesh fitting
// =========================================================================

namespace
{
	AActor* FindActorInWorlds(const FString& Label, bool bPreferPlayWorld, bool& bOutPlayWorld)
	{
		bOutPlayWorld = false;
		if (bPreferPlayWorld && GEditor && GEditor->PlayWorld)
		{
			for (TActorIterator<AActor> It(GEditor->PlayWorld); It; ++It)
			{
				if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase) || It->GetName().Equals(Label, ESearchCase::IgnoreCase))
				{
					bOutPlayWorld = true;
					return *It;
				}
			}
		}
		return FindActorByLabel(Label);
	}

	/** Blend of the per-bone skinning matrices for one vertex, normalized by its weight sum. */
	FMatrix BlendSkinning(const FBoneWeights& Weights, const TArray<FMatrix>& BoneSkinning)
	{
		FMatrix Blend(ForceInitToZero);
		float Sum = 0.f;
		for (int32 k = 0; k < Weights.Num(); ++k)
		{
			const int32 Bone = Weights[k].GetBoneIndex();
			const float W = Weights[k].GetWeight();
			if (W <= 0.f || !BoneSkinning.IsValidIndex(Bone)) { continue; }
			Blend += BoneSkinning[Bone] * W;
			Sum += W;
		}
		return Sum > 0.f ? Blend * (1.f / Sum) : FMatrix::Identity;
	}

	/** Where one vertex stands relative to another surface: signed distance along the nearest face's normal. */
	struct FClearance
	{
		int32 Vid = INDEX_NONE;
		bool bJudged = false;      // within MaxDistance and projecting onto the other surface (not past its open edge)
		double Signed = 0.0;       // > 0 outside (the side the other surface's normals face), < 0 inside
		FVector3d Normal = FVector3d::ZeroVector;
	};

	void EvaluateClearance(const FDynamicMesh3& M, const TArray<int32>& Vids, const FDynamicMesh3& Other, const FDynamicMeshAABBTree3& Tree, double MaxDistance, TArray<FClearance>& Out)
	{
		IMeshSpatial::FQueryOptions Query;
		Query.MaxDistance = MaxDistance;
		Out.Reset(Vids.Num());
		for (const int32 Vid : Vids)
		{
			FClearance C;
			C.Vid = Vid;
			if (M.IsVertex(Vid))
			{
				const FVector3d P = M.GetVertex(Vid);
				double DistSq = 0.0;
				const int32 Tid = Tree.FindNearestTriangle(P, DistSq, Query);
				if (Other.IsTriangle(Tid))
				{
					const FDistPoint3Triangle3d Dist = TMeshQueries<FDynamicMesh3>::TriangleDistance(Other, Tid, P);
					const FVector3d Delta = P - Dist.ClosestTrianglePoint;
					const FVector3d N = Other.GetTriNormal(Tid);
					const double Len = Delta.Length();
					// The offset must run along the face normal (within ~60 degrees): a vertex past an open edge (a head above a
					// collar) has its nearest point on that edge and a sideways offset, and is not covered by the surface.
					C.bJudged = Len <= MaxDistance && (Len < 1e-4 || FMath::Abs(Delta.Dot(N)) >= 0.5 * Len);
					C.Signed = Delta.Dot(N);
					C.Normal = N;
				}
			}
			Out.Add(C);
		}
	}

	/** Shared body of PushInside / PushOutside. */
	FModelingResult PushAgainst(int32 Handle, int32 OtherHandle, bool bInside, float Margin, const FString& SelectionName, float MaxDistance, int32 SmoothIterations)
	{
		UDynamicMesh* Mesh = FindMesh(Handle);
		if (!Mesh) { return NoMesh(Handle); }
		UDynamicMesh* OtherMesh = FindMesh(OtherHandle);
		if (!OtherMesh) { return Fail(Handle, FString::Printf(TEXT("No session mesh with handle %d to fit against"), OtherHandle)); }
		if (OtherMesh == Mesh) { return Fail(Handle, TEXT("A mesh cannot be fitted against itself")); }
		TArray<int32> Vids;
		FString Error;
		if (!SelectionVertices(Handle, SelectionName, Mesh, Vids, Error)) { return Fail(Handle, Error); }
		const TSet<int32> Allowed(Vids);
		const double M = FMath::Max(0.f, Margin);
		const int32 Rings = FMath::Clamp(SmoothIterations, 0, 20);

		FDynamicMesh3 Other;
		OtherMesh->ProcessMesh([&Other](const FDynamicMesh3& O) { Other.Copy(O, false, false, false, false); });   // positions and triangles only: a body LOD0 with all its attributes is tens of MB
		const FDynamicMeshAABBTree3 Tree(&Other, true);

		int32 Pushed = 0, Feathered = 0, Remaining = 0;
		double MaxPush = 0.0;
		Mesh->EditMesh([&](FDynamicMesh3& Target)
		{
			// Push along the nearest face's normal; a moved vertex can land over a neighbouring face with another normal, so
			// repeat on what is still wrong (a few passes settle it).
			TArray<FClearance> Clearance;
			TMap<int32, FVector3d> Displacement;
			for (int32 Pass = 0; Pass < 6; ++Pass)
			{
				EvaluateClearance(Target, Vids, Other, Tree, MaxDistance, Clearance);
				int32 Moved = 0;
				for (const FClearance& C : Clearance)
				{
					if (!C.bJudged) { continue; }
					const double Need = bInside ? C.Signed + M : M - C.Signed;   // how far it must travel to satisfy the rule
					if (Need <= (Pass == 0 ? 1e-6 : 1e-4)) { continue; }
					const FVector3d Step = (bInside ? -C.Normal : C.Normal) * Need;
					Target.SetVertex(C.Vid, Target.GetVertex(C.Vid) + Step);
					Displacement.FindOrAdd(C.Vid, FVector3d::ZeroVector) += Step;
					++Moved;
				}
				if (Moved == 0) { break; }
			}
			for (const TPair<int32, FVector3d>& Pair : Displacement) { MaxPush = FMath::Max(MaxPush, Pair.Value.Length()); }
			Pushed = Displacement.Num();

			// Feather: the rings around the pushed vertices take a smoothed share of their move (pushed vertices keep theirs).
			TSet<int32> Region;
			TArray<int32> Frontier;
			Displacement.GetKeys(Frontier);
			for (int32 Ring = 0; Ring < Rings; ++Ring)
			{
				TArray<int32> Next;
				for (const int32 Vid : Frontier)
				{
					for (const int32 Nbr : Target.VtxVerticesItr(Vid))
					{
						if (!Displacement.Contains(Nbr) && Allowed.Contains(Nbr) && !Region.Contains(Nbr)) { Region.Add(Nbr); Next.Add(Nbr); }
					}
				}
				Frontier = MoveTemp(Next);
			}
			TMap<int32, FVector3d> Soft;
			for (int32 Iter = 0; Iter < Rings; ++Iter)
			{
				TMap<int32, FVector3d> Step;
				for (const int32 Vid : Region)
				{
					FVector3d Sum = FVector3d::ZeroVector;
					int32 Count = 0;
					for (const int32 Nbr : Target.VtxVerticesItr(Vid))
					{
						if (const FVector3d* Hard = Displacement.Find(Nbr)) { Sum += *Hard; }
						else if (const FVector3d* Prev = Soft.Find(Nbr)) { Sum += *Prev; }
						++Count;
					}
					if (Count > 0) { Step.Add(Vid, Sum / Count); }
				}
				Soft = MoveTemp(Step);
			}
			for (const TPair<int32, FVector3d>& Pair : Soft)
			{
				if (Pair.Value.SquaredLength() > 1e-12) { Target.SetVertex(Pair.Key, Target.GetVertex(Pair.Key) + Pair.Value); ++Feathered; }
			}

			// What is still wrong after the move (the nearest face can change under a vertex that moved).
			EvaluateClearance(Target, Vids, Other, Tree, MaxDistance, Clearance);
			for (const FClearance& C : Clearance)
			{
				if (C.bJudged && (bInside ? C.Signed + M : M - C.Signed) > 0.01) { ++Remaining; }
			}
		}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions);

		return Ok(Handle, Mesh, FString::Printf(TEXT("Pushed %d vertices %s handle %d (margin %.2f cm, largest move %.2f cm), feathered %d neighbour(s) over %d ring(s); %d still within the margin%s"),
			Pushed, bInside ? TEXT("inside") : TEXT("outside"), OtherHandle, M, MaxPush, Feathered, Rings, Remaining,
			Remaining > 0 ? TEXT(" (run again, or raise smooth_iterations)") : TEXT("")));
	}
}

FModelingResult UModelingService::LoadMeshFromSkinnedComponent(const FString& ActorLabel, const FString& ComponentName, bool bWorldSpace, int32 LODIndex, bool bPreferPlayWorld)
{
	bool bPlayWorld = false;
	AActor* Actor = FindActorInWorlds(ActorLabel, bPreferPlayWorld, bPlayWorld);
	if (!Actor) { return Fail(-1, FString::Printf(TEXT("No actor labeled '%s' in the %s"), *ActorLabel, bPreferPlayWorld && GEditor && GEditor->PlayWorld ? TEXT("PIE or editor world") : TEXT("editor world"))); }
	TArray<USkinnedMeshComponent*> Components;
	Actor->GetComponents(Components);
	TArray<FString> Names;
	USkinnedMeshComponent* Component = nullptr;
	for (USkinnedMeshComponent* Candidate : Components)
	{
		Names.Add(Candidate->GetName());
		if (!Component && (ComponentName.IsEmpty() || Candidate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))) { Component = Candidate; }
	}
	if (!Component)
	{
		return Fail(-1, FString::Printf(TEXT("Actor '%s' has no skinned component%s (has: %s)"), *ActorLabel,
			ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName), Names.Num() ? *FString::Join(Names, TEXT(", ")) : TEXT("none")));
	}
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Component->GetSkinnedAsset());
	if (!SkeletalMesh) { return Fail(-1, FString::Printf(TEXT("Component '%s' has no SkeletalMesh assigned"), *Component->GetName())); }
	const USkinnedMeshComponent* PoseSource = Component->LeaderPoseComponent.IsValid() ? Component->LeaderPoseComponent.Get() : Component;
	if (PoseSource->GetNumComponentSpaceTransforms() == 0)
	{
		return Fail(-1, FString::Printf(TEXT("'%s' has no evaluated pose yet (component not registered, or its leader '%s' has not ticked): open the level / let PIE run a frame"),
			*Component->GetName(), *PoseSource->GetName()));
	}

	const FModelingResult Loaded = LoadMeshFromSkeletalMesh(SkeletalMesh->GetPathName(), LODIndex);
	if (!Loaded.bSuccess) { return Loaded; }
	const int32 Handle = Loaded.Handle;
	UDynamicMesh* Mesh = FindMesh(Handle);

	// Runtime skinning: InvRefBase(bone) * ComponentSpace(bone), per bone of the mesh's weight indices.
	const FTransform LocalToWorld = bWorldSpace ? Component->GetComponentTransform() : FTransform::Identity;
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	const TArray<FMatrix44f>& InvRef = SkeletalMesh->GetRefBasesInvMatrix();
	FPoseBinding Binding;
	int32 Unmatched = 0;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		const bool bHasBones = M.HasAttributes() && M.Attributes()->HasBones();
		const int32 NumBones = bHasBones ? M.Attributes()->GetNumBones() : RefSkeleton.GetRawBoneNum();
		Binding.BoneSkinning.SetNum(NumBones);
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FName Name = bHasBones ? M.Attributes()->GetBoneNames()->GetValue(i) : RefSkeleton.GetBoneName(i);
			const int32 AssetBone = RefSkeleton.FindBoneIndex(Name);
			const int32 ComponentBone = Component->GetBoneIndex(Name);
			if (AssetBone == INDEX_NONE || ComponentBone == INDEX_NONE || !InvRef.IsValidIndex(AssetBone))
			{
				Binding.BoneSkinning[i] = LocalToWorld.ToMatrixWithScale();   // unknown bone: stays in the reference pose
				++Unmatched;
				continue;
			}
			Binding.BoneSkinning[i] = FMatrix(InvRef[AssetBone]) * Component->GetBoneTransform(ComponentBone, LocalToWorld).ToMatrixWithScale();
		}
	});
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		const FSkinWeights* Skin = DefaultSkinWeights(M);
		FBoneWeights Weights;
		for (const int32 Vid : M.VertexIndicesItr())
		{
			if (Skin) { Skin->GetValue(Vid, Weights); }
			const FMatrix Blend = Skin ? BlendSkinning(Weights, Binding.BoneSkinning) : LocalToWorld.ToMatrixWithScale();
			M.SetVertex(Vid, Blend.TransformPosition(M.GetVertex(Vid)));
		}
	}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions);
	Binding.Source = FString::Printf(TEXT("%s.%s LOD %d, %s world, %s space"), *ActorLabel, *Component->GetName(), LODIndex,
		bPlayWorld ? TEXT("PIE") : TEXT("editor"), bWorldSpace ? TEXT("world") : TEXT("component"));
	Session().Poses.Add(Handle, Binding);

	FModelingResult R = Ok(Handle, Mesh, FString::Printf(TEXT("Loaded %s posed (%s%s), %d bone(s)%s; components on the actor: %s. Fit it, then unpose_mesh before save_mesh_to_skeletal_mesh_lod"),
		*SkeletalMesh->GetPathName(), *Binding.Source, PoseSource != Component ? *FString::Printf(TEXT(", pose from leader '%s'"), *PoseSource->GetName()) : TEXT(""),
		Binding.BoneSkinning.Num(), Unmatched ? *FString::Printf(TEXT(", %d not on the component (left in the reference pose)"), Unmatched) : TEXT(""), *FString::Join(Names, TEXT(", "))));
	R.AssetPath = SkeletalMesh->GetPathName();
	return R;
}

FModelingResult UModelingService::UnposeMesh(int32 Handle)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	const FPoseBinding* Binding = Session().Poses.Find(Handle);
	if (!Binding) { return Fail(Handle, FString::Printf(TEXT("Handle %d is not posed (only load_mesh_from_skinned_component handles are)"), Handle)); }
	int32 NumBones = 0;
	Mesh->ProcessMesh([&NumBones](const FDynamicMesh3& M) { NumBones = (M.HasAttributes() && M.Attributes()->HasBones()) ? M.Attributes()->GetNumBones() : NumBones; });
	if (NumBones != 0 && NumBones != Binding->BoneSkinning.Num())
	{
		return Fail(Handle, FString::Printf(TEXT("The handle's bones changed since the pose was read (%d -> %d): unpose before replacing bones"), Binding->BoneSkinning.Num(), NumBones));
	}
	int32 Singular = 0;
	Mesh->EditMesh([&](FDynamicMesh3& M)
	{
		const FSkinWeights* Skin = DefaultSkinWeights(M);
		FBoneWeights Weights;
		for (const int32 Vid : M.VertexIndicesItr())
		{
			if (Skin) { Skin->GetValue(Vid, Weights); }
			const FMatrix Blend = Skin ? BlendSkinning(Weights, Binding->BoneSkinning) : FMatrix::Identity;
			if (FMath::Abs(Blend.Determinant()) < UE_SMALL_NUMBER) { ++Singular; continue; }
			M.SetVertex(Vid, Blend.InverseFast().TransformPosition(M.GetVertex(Vid)));
		}
	}, EDynamicMeshChangeType::DeformationEdit, EDynamicMeshAttributeChangeFlags::VertexPositions);
	const FString Source = Binding->Source;
	Session().Poses.Remove(Handle);
	return Ok(Handle, Mesh, FString::Printf(TEXT("Back in the reference pose (was %s)%s"), *Source,
		Singular ? *FString::Printf(TEXT("; %d vertices with a degenerate blend left where they were"), Singular) : TEXT("")));
}

FModelingPenetrationReport UModelingService::MeasurePenetration(int32 Handle, int32 OtherHandle, const FString& Side, float Margin, float MaxDistance,
	const FString& SelectionName, const FString& OutSelectionName)
{
	FModelingPenetrationReport Report;
	UDynamicMesh* Mesh = FindMesh(Handle);
	UDynamicMesh* OtherMesh = FindMesh(OtherHandle);
	if (!Mesh || !OtherMesh) { Report.Message = FString::Printf(TEXT("No session mesh with handle %d"), Mesh ? OtherHandle : Handle); return Report; }
	if (Mesh == OtherMesh) { Report.Message = TEXT("A mesh cannot be measured against itself"); return Report; }
	const bool bInside = Side.TrimStartAndEnd().Equals(TEXT("Inside"), ESearchCase::IgnoreCase);
	if (!bInside && !Side.TrimStartAndEnd().Equals(TEXT("Outside"), ESearchCase::IgnoreCase))
	{
		Report.Message = FString::Printf(TEXT("Unknown side '%s' (use Inside: this mesh belongs under the other, or Outside: over it)"), *Side);
		return Report;
	}
	TArray<int32> Vids;
	FString Error;
	if (!SelectionVertices(Handle, SelectionName, Mesh, Vids, Error)) { Report.Message = Error; return Report; }
	FDynamicMesh3 Other;
	OtherMesh->ProcessMesh([&Other](const FDynamicMesh3& O) { Other.Copy(O, false, false, false, false); });   // positions and triangles only: a body LOD0 with all its attributes is tens of MB
	const FDynamicMeshAABBTree3 Tree(&Other, true);
	TArray<FClearance> Clearance;
	TArray<int32> Offending;
	double SumDepth = 0.0;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		EvaluateClearance(M, Vids, Other, Tree, MaxDistance, Clearance);
		for (const FClearance& C : Clearance)
		{
			if (!C.bJudged) { continue; }
			++Report.VerticesChecked;
			const double Depth = bInside ? C.Signed + Margin : Margin - C.Signed;
			if (Depth <= 1e-4) { continue; }
			++Report.Violations;
			Offending.Add(C.Vid);
			SumDepth += Depth;
			if (Depth > Report.MaxDepth)
			{
				Report.MaxDepth = static_cast<float>(Depth);
				Report.WorstVertex = C.Vid;
				Report.WorstPosition = M.GetVertex(C.Vid);
			}
		}
	});
	Report.AverageDepth = Report.Violations ? static_cast<float>(SumDepth / Report.Violations) : 0.f;
	if (!OutSelectionName.IsEmpty())
	{
		FGeometryScriptMeshSelection Selection;
		UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(Mesh, Offending, EGeometryScriptMeshSelectionType::Vertices, Selection);
		StoreSelection(Handle, OutSelectionName, Selection);
	}
	Report.bSuccess = true;
	Report.Message = FString::Printf(TEXT("%d of %d judged vertices of handle %d break '%s handle %d by %.2f cm' (max %.2f cm at %s, average %.2f cm); %d vertices not covered or farther than %.1f cm were skipped%s"),
		Report.Violations, Report.VerticesChecked, Handle, bInside ? TEXT("inside") : TEXT("outside"), OtherHandle, Margin, Report.MaxDepth,
		*Report.WorstPosition.ToCompactString(), Report.AverageDepth, Vids.Num() - Report.VerticesChecked, MaxDistance,
		OutSelectionName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; offenders stored as selection '%s'"), *OutSelectionName));
	return Report;
}

FModelingResult UModelingService::PushInside(int32 Handle, int32 OtherHandle, float Margin, const FString& SelectionName, float MaxDistance, int32 SmoothIterations)
{
	return PushAgainst(Handle, OtherHandle, true, Margin, SelectionName, MaxDistance, SmoothIterations);
}

FModelingResult UModelingService::PushOutside(int32 Handle, int32 OtherHandle, float Margin, const FString& SelectionName, float MaxDistance, int32 SmoothIterations)
{
	return PushAgainst(Handle, OtherHandle, false, Margin, SelectionName, MaxDistance, SmoothIterations);
}

int32 UModelingService::SelectHiddenBy(int32 Handle, const FString& SelectionName, int32 OtherHandle, float Margin, float MaxDistance, bool bAllVertices)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	UDynamicMesh* OtherMesh = FindMesh(OtherHandle);
	if (!Mesh || !OtherMesh || Mesh == OtherMesh || SelectionName.IsEmpty()) { return -1; }
	FDynamicMesh3 Other;
	OtherMesh->ProcessMesh([&Other](const FDynamicMesh3& O) { Other.Copy(O, false, false, false, false); });   // positions and triangles only: a body LOD0 with all its attributes is tens of MB
	const FDynamicMeshAABBTree3 Tree(&Other, true);
	TArray<int32> Triangles;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		TArray<int32> Vids;
		for (const int32 Vid : M.VertexIndicesItr()) { Vids.Add(Vid); }
		TArray<FClearance> Clearance;
		EvaluateClearance(M, Vids, Other, Tree, MaxDistance, Clearance);
		TSet<int32> Hidden;
		for (const FClearance& C : Clearance) { if (C.bJudged && C.Signed <= -Margin) { Hidden.Add(C.Vid); } }
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			const FIndex3i T = M.GetTriangle(Tid);
			const int32 Count = (Hidden.Contains(T.A) ? 1 : 0) + (Hidden.Contains(T.B) ? 1 : 0) + (Hidden.Contains(T.C) ? 1 : 0);
			if (bAllVertices ? Count == 3 : Count > 0) { Triangles.Add(Tid); }
		}
	});
	FGeometryScriptMeshSelection Selection;
	UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(Mesh, Triangles, EGeometryScriptMeshSelectionType::Triangles, Selection);
	StoreSelection(Handle, SelectionName, Selection);
	return Triangles.Num();
}

FModelingResult UModelingService::SetAssetMaterials(const FString& AssetPath, const FString& MaterialPaths, bool bSaveAsset)
{
	TArray<FString> Paths;
	MaterialPaths.ParseIntoArray(Paths, TEXT(","), true);
	TArray<UMaterialInterface*> Materials;
	for (FString& Path : Paths)
	{
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty()) { continue; }
		UMaterialInterface* Material = LoadAssetAs<UMaterialInterface>(Path);
		if (!Material) { return Fail(-1, FString::Printf(TEXT("Material not found: %s"), *Path)); }
		Materials.Add(Material);
	}
	if (Materials.Num() == 0) { return Fail(-1, TEXT("MaterialPaths must list at least one material asset path (comma-separated, in slot order)")); }

	UObject* Asset = UEditorAssetLibrary::DoesAssetExist(AssetPath) ? UEditorAssetLibrary::LoadAsset(AssetPath) : nullptr;
	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
	{
		TArray<FStaticMaterial> Slots = StaticMesh->GetStaticMaterials();
		Slots.SetNum(FMath::Max(Slots.Num(), Materials.Num()));
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			Slots[i].MaterialInterface = Materials[i];
			if (Slots[i].MaterialSlotName.IsNone()) { Slots[i].MaterialSlotName = *FString::Printf(TEXT("Slot%d"), i); }
		}
		StaticMesh->SetStaticMaterials(Slots);
		StaticMesh->MarkPackageDirty();
		StaticMesh->PostEditChange();
	}
	else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
	{
		TArray<FSkeletalMaterial> Slots = SkeletalMesh->GetMaterials();
		Slots.SetNum(FMath::Max(Slots.Num(), Materials.Num()));
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			Slots[i].MaterialInterface = Materials[i];
			if (Slots[i].MaterialSlotName.IsNone()) { Slots[i].MaterialSlotName = *FString::Printf(TEXT("Slot%d"), i); }
		}
		SkeletalMesh->SetMaterials(Slots);
		SkeletalMesh->MarkPackageDirty();
		SkeletalMesh->PostEditChange();
	}
	else
	{
		return Fail(-1, FString::Printf(TEXT("%s is not a StaticMesh or SkeletalMesh asset"), *AssetPath));
	}
	SaveIf(bSaveAsset, AssetPath);
	FModelingResult R;
	R.bSuccess = true;
	R.AssetPath = AssetPath;
	R.Message = FString::Printf(TEXT("Assigned %d material slot(s) on %s"), Materials.Num(), *AssetPath);
	return R;
}

// =========================================================================
// UV control
// =========================================================================

namespace
{
	FGeometryScriptGroupLayer DefaultGroupLayer()
	{
		FGeometryScriptGroupLayer Layer;
		Layer.bDefaultLayer = true;
		return Layer;
	}

	/** Case-insensitive enum lookup so "Polygroups" finds PolyGroups. */
	template <typename TEnum>
	bool ParseEnumLoose(const FString& Name, TEnum& Out)
	{
		const UEnum* E = StaticEnum<TEnum>();
		const FString Wanted = Name.TrimStartAndEnd();
		for (int32 i = 0; i < E->NumEnums() - 1; ++i)
		{
			if (E->GetNameStringByIndex(i).Equals(Wanted, ESearchCase::IgnoreCase))
			{
				Out = static_cast<TEnum>(E->GetValueByIndex(i));
				return true;
			}
		}
		return false;
	}

	int32 NumUVLayers(UDynamicMesh* Mesh)
	{
		int32 Num = 0;
		Mesh->ProcessMesh([&Num](const FDynamicMesh3& M) { Num = M.HasAttributes() ? M.Attributes()->NumUVLayers() : 0; });
		return Num;
	}

	FGeometryScriptMeshSelection SelectionForMaterial(UDynamicMesh* Mesh, int32 MaterialID)
	{
		TArray<int32> TriangleIDs;
		Mesh->ProcessMesh([&](const FDynamicMesh3& M)
		{
			if (!M.HasAttributes() || !M.Attributes()->HasMaterialID()) { return; }
			const FDynamicMeshMaterialAttribute* Materials = M.Attributes()->GetMaterialID();
			for (const int32 Tid : M.TriangleIndicesItr())
			{
				if (Materials->GetValue(Tid) == MaterialID) { TriangleIDs.Add(Tid); }
			}
		});
		FGeometryScriptMeshSelection Selection;
		UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(Mesh, TriangleIDs, EGeometryScriptMeshSelectionType::Triangles, Selection);
		return Selection;
	}
}

FModelingResult UModelingService::SetPolygroup(int32 Handle, const FString& SelectionName, int32 GroupID)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection;
	FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	Mesh->EditMesh([](FDynamicMesh3& M)
	{
		if (!M.HasTriangleGroups()) { M.EnableTriangleGroups(0); }
	});
	UGeometryScriptDebug* Debug = NewDebug();
	int32 SetID = 0;
	UGeometryScriptLibrary_MeshPolygroupFunctions::SetPolygroupForMeshSelection(Mesh, DefaultGroupLayer(), Selection, SetID, FMath::Max(0, GroupID), GroupID < 0, false);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Selection '%s' is now polygroup %d"), *SelectionName, SetID));
}

FModelingResult UModelingService::RecomputeUVs(int32 Handle, int32 UVLayer, const FString& IslandSource, const FString& Method, const FString& SelectionName, bool bAlignIslandsWithAxes)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection;
	FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	FGeometryScriptRecomputeUVsOptions Options;
	if (!ParseEnumLoose(IslandSource, Options.IslandSource))
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown island source '%s' (use %s)"), *IslandSource, *EnumNames<EGeometryScriptUVIslandSource>()));
	}
	if (!ParseEnumLoose(Method, Options.Method))
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown unwrap method '%s' (use %s)"), *Method, *EnumNames<EGeometryScriptUVFlattenMethod>()));
	}
	Options.GroupLayer = DefaultGroupLayer();
	Options.bAutoAlignIslandsWithAxes = bAlignIslandsWithAxes;
	if (UVLayer >= NumUVLayers(Mesh)) { SetNumUVLayers(Handle, UVLayer + 1); }
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshUVFunctions::RecomputeMeshUVs(Mesh, UVLayer, Options, Selection, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Recomputed UV layer %d from %s (%s) - islands are not packed yet, run layout_uv"), UVLayer, *IslandSource, *Method));
}

FModelingResult UModelingService::TransformUV(int32 Handle, int32 UVLayer, const FString& SelectionName, FVector2D Translation, FVector2D Scale, float RotationDeg, FVector2D Origin)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection;
	FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	if (UVLayer < 0 || UVLayer >= NumUVLayers(Mesh)) { return Fail(Handle, FString::Printf(TEXT("UV layer %d does not exist (mesh has %d)"), UVLayer, NumUVLayers(Mesh))); }
	UGeometryScriptDebug* Debug = NewDebug();
	if (!Scale.Equals(FVector2D(1, 1)))
	{
		UGeometryScriptLibrary_MeshUVFunctions::ScaleMeshUVs(Mesh, UVLayer, Scale, Origin, Selection, Debug);
	}
	if (!FMath::IsNearlyZero(RotationDeg))
	{
		UGeometryScriptLibrary_MeshUVFunctions::RotateMeshUVs(Mesh, UVLayer, RotationDeg, Origin, Selection, Debug);
	}
	if (!Translation.IsNearlyZero())
	{
		UGeometryScriptLibrary_MeshUVFunctions::TranslateMeshUVs(Mesh, UVLayer, Translation, Selection, Debug);
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Transformed UVs of '%s' in layer %d"), *SelectionName, UVLayer));
}

FModelingResult UModelingService::LayoutUV(int32 Handle, int32 UVLayer, const FString& LayoutType, const FString& SelectionName, int32 TextureResolution, float Scale, FVector2D Translation)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	FGeometryScriptMeshSelection Selection;
	FString Error;
	if (!ResolveSelection(Handle, SelectionName, Mesh, Selection, Error)) { return Fail(Handle, Error); }
	if (UVLayer < 0 || UVLayer >= NumUVLayers(Mesh)) { return Fail(Handle, FString::Printf(TEXT("UV layer %d does not exist (mesh has %d)"), UVLayer, NumUVLayers(Mesh))); }
	FGeometryScriptLayoutUVsOptions Options;
	if (!ParseEnumLoose(LayoutType, Options.LayoutType))
	{
		return Fail(Handle, FString::Printf(TEXT("Unknown layout type '%s' (use %s)"), *LayoutType, *EnumNames<EGeometryScriptUVLayoutType>()));
	}
	Options.TextureResolution = FMath::Clamp(TextureResolution, 16, 16384);
	Options.Scale = Scale;
	Options.Translation = Translation;
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshUVFunctions::LayoutMeshUVs(Mesh, UVLayer, Options, Selection, Debug);
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("%s layout of UV layer %d"), *LayoutType, UVLayer));
}

FModelingResult UModelingService::PackUVPerMaterial(int32 Handle, int32 UVLayer, int32 TextureResolution)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (UVLayer < 0 || UVLayer >= NumUVLayers(Mesh)) { return Fail(Handle, FString::Printf(TEXT("UV layer %d does not exist (mesh has %d)"), UVLayer, NumUVLayers(Mesh))); }
	TSet<int32> MaterialIDs;
	Mesh->ProcessMesh([&MaterialIDs](const FDynamicMesh3& M)
	{
		if (!M.HasAttributes() || !M.Attributes()->HasMaterialID()) { return; }
		for (const int32 Tid : M.TriangleIndicesItr()) { MaterialIDs.Add(M.Attributes()->GetMaterialID()->GetValue(Tid)); }
	});
	if (MaterialIDs.Num() == 0) { return Fail(Handle, TEXT("Mesh has no material IDs (set_material_id first) - use layout_uv for a single-material pack")); }
	FGeometryScriptLayoutUVsOptions Options;
	Options.LayoutType = EGeometryScriptUVLayoutType::Repack;
	Options.TextureResolution = FMath::Clamp(TextureResolution, 16, 16384);
	UGeometryScriptDebug* Debug = NewDebug();
	for (const int32 MaterialID : MaterialIDs)
	{
		UGeometryScriptLibrary_MeshUVFunctions::LayoutMeshUVs(Mesh, UVLayer, Options, SelectionForMaterial(Mesh, MaterialID), Debug);
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Packed UV layer %d separately for %d material ID(s) - each slot now uses the full 0-1 range"), UVLayer, MaterialIDs.Num()));
}

FModelingUVStats UModelingService::GetUVStats(int32 Handle, int32 UVLayer, int32 GridResolution)
{
	FModelingUVStats Stats;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { Stats.Message = NoMesh(Handle).Message; return Stats; }
	if (UVLayer < 0 || UVLayer >= NumUVLayers(Mesh)) { Stats.Message = FString::Printf(TEXT("UV layer %d does not exist (mesh has %d)"), UVLayer, NumUVLayers(Mesh)); return Stats; }

	FGeometryScriptMeshSelection All;
	UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(Mesh, All, EGeometryScriptMeshSelectionType::Triangles);
	double MeshArea = 0.0, UVArea = 0.0;
	FBox MeshBounds(ForceInit);
	FBox2D UVBounds(ForceInit);
	bool bValid = false, bUnset = false;
	UGeometryScriptLibrary_MeshUVFunctions::GetMeshUVSizeInfo(Mesh, UVLayer, All, MeshArea, UVArea, MeshBounds, UVBounds, bValid, bUnset, true, nullptr);
	Stats.MeshArea = static_cast<float>(MeshArea);
	Stats.UVArea = static_cast<float>(UVArea);
	Stats.UVBoundsMin = UVBounds.Min;
	Stats.UVBoundsMax = UVBounds.Max;
	Stats.TexelsPerCmAt1K = MeshArea > 0.0 ? static_cast<float>(FMath::Sqrt(UVArea / MeshArea) * 1024.0) : 0.f;

	const int32 Res = FMath::Clamp(GridResolution, 16, 4096);
	TArray<uint8> Hits;
	Hits.SetNumZeroed(Res * Res);
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		const FDynamicMeshUVOverlay* UV = M.Attributes()->GetUVLayer(UVLayer);
		if (!UV) { return; }
		FMeshConnectedComponents Components(&M);
		Components.FindConnectedTriangles([UV](int32 T0, int32 T1) { return UV->AreTrianglesConnected(T0, T1); });
		Stats.NumIslands = Components.Num();
		for (const int32 Tid : M.TriangleIndicesItr())
		{
			if (!UV->IsSetTriangle(Tid)) { ++Stats.NumUnsetTriangles; continue; }
			FVector2f A, B, C;
			UV->GetTriElements(Tid, A, B, C);
			const FVector2f P0 = A * Res, P1 = B * Res, P2 = C * Res;
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(FMath::Min3(P0.X, P1.X, P2.X)), 0, Res - 1), X1 = FMath::Clamp(FMath::CeilToInt(FMath::Max3(P0.X, P1.X, P2.X)), 0, Res - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FMath::Min3(P0.Y, P1.Y, P2.Y)), 0, Res - 1), Y1 = FMath::Clamp(FMath::CeilToInt(FMath::Max3(P0.Y, P1.Y, P2.Y)), 0, Res - 1);
			const float Area = (P1.X - P0.X) * (P2.Y - P0.Y) - (P2.X - P0.X) * (P1.Y - P0.Y);
			if (FMath::Abs(Area) < 1e-6f) { continue; }
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector2f P(X + 0.5f, Y + 0.5f);
					const float W0 = ((P1.X - P.X) * (P2.Y - P.Y) - (P2.X - P.X) * (P1.Y - P.Y)) / Area;
					const float W1 = ((P2.X - P.X) * (P0.Y - P.Y) - (P0.X - P.X) * (P2.Y - P.Y)) / Area;
					const float W2 = 1.f - W0 - W1;
					if (W0 >= 0.f && W1 >= 0.f && W2 >= 0.f)
					{
						uint8& Cell = Hits[Y * Res + X];
						if (Cell < 255) { ++Cell; }
					}
				}
			}
		}
	});
	int32 Covered = 0, Overlapped = 0;
	for (const uint8 Cell : Hits)
	{
		if (Cell > 0) { ++Covered; }
		if (Cell > 1) { ++Overlapped; }
	}
	Stats.Coverage = static_cast<float>(Covered) / static_cast<float>(Res * Res);
	Stats.OverlapFraction = Covered > 0 ? static_cast<float>(Overlapped) / static_cast<float>(Covered) : 0.f;
	Stats.bSuccess = true;
	Stats.Message = FString::Printf(TEXT("%d island(s), %.0f%% of 0-1 covered, %.1f%% overlapping, %d unset triangle(s), %.2f texels/cm at 1K"),
		Stats.NumIslands, Stats.Coverage * 100.f, Stats.OverlapFraction * 100.f, Stats.NumUnsetTriangles, Stats.TexelsPerCmAt1K);
	return Stats;
}

bool UModelingService::WorldToUV(int32 Handle, FVector Point, int32 UVLayer, FVector2D& OutUV)
{
	OutUV = FVector2D::ZeroVector;
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh || UVLayer < 0 || UVLayer >= NumUVLayers(Mesh)) { return false; }
	UGeometryScriptDebug* Debug = NewDebug();
	FGeometryScriptDynamicMeshBVH BVH;
	UGeometryScriptLibrary_MeshSpatial::BuildBVHForMesh(Mesh, BVH, Debug);
	FGeometryScriptTrianglePoint Nearest;
	EGeometryScriptSearchOutcomePins Outcome = EGeometryScriptSearchOutcomePins::NotFound;
	UGeometryScriptLibrary_MeshSpatial::FindNearestPointOnMesh(Mesh, BVH, Point, FGeometryScriptSpatialQueryOptions(), Nearest, Outcome, Debug);
	if (Outcome != EGeometryScriptSearchOutcomePins::Found || !Nearest.bValid) { return false; }
	bool bFound = false;
	Mesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		const FDynamicMeshUVOverlay* UV = M.Attributes()->GetUVLayer(UVLayer);
		if (!UV || !UV->IsSetTriangle(Nearest.TriangleID)) { return; }
		FVector2f A, B, C;
		UV->GetTriElements(Nearest.TriangleID, A, B, C);
		const FVector2f Result = A * static_cast<float>(Nearest.BaryCoords.X) + B * static_cast<float>(Nearest.BaryCoords.Y) + C * static_cast<float>(Nearest.BaryCoords.Z);
		OutUV = FVector2D(Result.X, Result.Y);
		bFound = true;
	});
	return bFound;
}

// =========================================================================
// Image tools
// =========================================================================

namespace
{
	/** Perlin fBm that wraps at the image borders (blend of the four torus-shifted lookups). */
	float TileableNoise(float U, float V, float Cells, const FVector2D& Offset)
	{
		const FVector2D P(U * Cells, V * Cells);
		const float N00 = FMath::PerlinNoise2D(P + Offset);
		const float N10 = FMath::PerlinNoise2D(P - FVector2D(Cells, 0) + Offset);
		const float N01 = FMath::PerlinNoise2D(P - FVector2D(0, Cells) + Offset);
		const float N11 = FMath::PerlinNoise2D(P - FVector2D(Cells, Cells) + Offset);
		return FMath::Lerp(FMath::Lerp(N00, N10, U), FMath::Lerp(N01, N11, U), V);
	}

	void FinishTextureEdit(UTexture2D* Texture, bool bSaveAsset)
	{
		Texture->UpdateResource();
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		Texture->BlockOnAnyAsyncBuild();
		SaveIf(bSaveAsset, Texture->GetPathName());
	}
}

FModelingResult UModelingService::CreateNoiseTexture(const FString& AssetPath, int32 Width, int32 Height, float Scale, int32 Octaves, float Persistence, int32 Seed, bool bSaveAsset)
{
	if (!AssetPath.StartsWith(TEXT("/"))) { return Fail(-1, FString::Printf(TEXT("AssetPath must be a content path like /Game/Textures/T_Name (got '%s')"), *AssetPath)); }
	const int32 W = FMath::Clamp(Width, 4, 8192), H = FMath::Clamp(Height, 4, 8192);
	const int32 NumOctaves = FMath::Clamp(Octaves, 1, 10);
	const float Cells = FMath::Max(1.f, Scale);
	FRandomStream Random(Seed);
	const FVector2D Offset(Random.FRandRange(0.f, 1000.f), Random.FRandRange(0.f, 1000.f));

	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(W * H * 4);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			float Value = 0.f, Amplitude = 1.f, Total = 0.f, Frequency = Cells;
			for (int32 O = 0; O < NumOctaves; ++O)
			{
				Value += Amplitude * TileableNoise(static_cast<float>(X) / W, static_cast<float>(Y) / H, Frequency, Offset * (O + 1));
				Total += Amplitude;
				Amplitude *= Persistence;
				Frequency *= 2.f;
			}
			const uint8 Gray = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((0.5f + 0.5f * Value / Total) * 255.f), 0, 255));
			uint8* P = &Pixels[(Y * W + X) * 4];
			P[0] = Gray; P[1] = Gray; P[2] = Gray; P[3] = 255;
		}
	}

	UTexture2D* Texture = UEditorAssetLibrary::DoesAssetExist(AssetPath) ? LoadAssetAs<UTexture2D>(AssetPath) : nullptr;
	if (!Texture)
	{
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath)) { return Fail(-1, FString::Printf(TEXT("%s exists and is not a Texture2D"), *AssetPath)); }
		UPackage* Package = CreatePackage(*AssetPath);
		Texture = NewObject<UTexture2D>(Package, *FPackageName::GetLongPackageAssetName(AssetPath), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Texture->Source.Init(W, H, 1, 1, TSF_BGRA8, Pixels.GetData());
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_Grayscale;
	FinishTextureEdit(Texture, bSaveAsset);
	FModelingResult R;
	R.bSuccess = true;
	R.AssetPath = Texture->GetPathName();
	R.Message = FString::Printf(TEXT("Noise texture %dx%d (%d octaves, %.0f cells, seed %d) at %s"), W, H, NumOctaves, Cells, Seed, *R.AssetPath);
	return R;
}

FModelingResult UModelingService::DrawOnTexture(const FString& AssetPath, const FString& Shape, const TArray<FVector2D>& Points, FLinearColor Color, float ThicknessPx, float SpacingPx, bool bSaveAsset)
{
	UTexture2D* Texture = LoadAssetAs<UTexture2D>(AssetPath);
	if (!Texture) { return Fail(-1, FString::Printf(TEXT("Texture2D not found: %s"), *AssetPath)); }
	FTextureSource& Source = Texture->Source;
	if (!Source.IsValid()) { return Fail(-1, FString::Printf(TEXT("%s has no editable source data"), *AssetPath)); }
	const ETextureSourceFormat Format = Source.GetFormat();
	if (Format != TSF_BGRA8 && Format != TSF_G8)
	{
		return Fail(-1, FString::Printf(TEXT("%s is not an 8-bit texture (BGRA8 or G8); draw into an imported PNG or a create_noise_texture result"), *AssetPath));
	}
	const FString Kind = Shape.TrimStartAndEnd();
	const bool bLine = Kind.Equals(TEXT("Line"), ESearchCase::IgnoreCase), bDots = Kind.Equals(TEXT("Dots"), ESearchCase::IgnoreCase),
		bRect = Kind.Equals(TEXT("Rect"), ESearchCase::IgnoreCase), bFill = Kind.Equals(TEXT("Fill"), ESearchCase::IgnoreCase);
	if (!bLine && !bDots && !bRect && !bFill) { return Fail(-1, FString::Printf(TEXT("Unknown shape '%s' (use Line, Dots, Rect, Fill)"), *Shape)); }
	if ((bLine || bDots) && Points.Num() < 2) { return Fail(-1, TEXT("Line and Dots need at least 2 points")); }
	if (bRect && Points.Num() < 2) { return Fail(-1, TEXT("Rect needs 2 points (opposite corners)")); }

	const int32 W = static_cast<int32>(Source.GetSizeX()), H = static_cast<int32>(Source.GetSizeY());
	const FColor Pixel = Color.ToFColor(Texture->SRGB);
	const uint8 Gray = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(0.299f * Pixel.R + 0.587f * Pixel.G + 0.114f * Pixel.B), 0, 255));
	uint8* Data = Source.LockMip(0, 0, 0);
	if (!Data) { return Fail(-1, FString::Printf(TEXT("Could not lock %s for writing"), *AssetPath)); }
	int64 Painted = 0;
	auto Plot = [&](int32 X, int32 Y)
	{
		if (X < 0 || Y < 0 || X >= W || Y >= H) { return; }
		if (Format == TSF_BGRA8)
		{
			uint8* P = Data + (static_cast<int64>(Y) * W + X) * 4;
			P[0] = Pixel.B; P[1] = Pixel.G; P[2] = Pixel.R; P[3] = Pixel.A;
		}
		else
		{
			Data[static_cast<int64>(Y) * W + X] = Gray;
		}
		++Painted;
	};
	auto Disc = [&](const FVector2D& Center, float Radius)
	{
		const float R = FMath::Max(0.5f, Radius);
		for (int32 Y = FMath::FloorToInt(Center.Y - R); Y <= FMath::CeilToInt(Center.Y + R); ++Y)
		{
			for (int32 X = FMath::FloorToInt(Center.X - R); X <= FMath::CeilToInt(Center.X + R); ++X)
			{
				if (FVector2D::DistSquared(FVector2D(X + 0.5f, Y + 0.5f), Center) <= R * R) { Plot(X, Y); }
			}
		}
	};
	auto ToPixels = [&](const FVector2D& UV) { return FVector2D(UV.X * W, UV.Y * H); };

	if (bFill)
	{
		for (int32 Y = 0; Y < H; ++Y) { for (int32 X = 0; X < W; ++X) { Plot(X, Y); } }
	}
	else if (bRect)
	{
		const FVector2D A = ToPixels(Points[0]), B = ToPixels(Points[1]);
		for (int32 Y = FMath::FloorToInt(FMath::Min(A.Y, B.Y)); Y < FMath::CeilToInt(FMath::Max(A.Y, B.Y)); ++Y)
		{
			for (int32 X = FMath::FloorToInt(FMath::Min(A.X, B.X)); X < FMath::CeilToInt(FMath::Max(A.X, B.X)); ++X) { Plot(X, Y); }
		}
	}
	else
	{
		const float Radius = FMath::Max(0.5f, ThicknessPx * 0.5f);
		const float Step = bDots ? FMath::Max(1.f, SpacingPx) : 0.5f;
		float Carry = 0.f;   // distance already travelled since the last stamp, carried across segments
		bool bFirst = true;
		for (int32 i = 0; i + 1 < Points.Num(); ++i)
		{
			const FVector2D A = ToPixels(Points[i]), B = ToPixels(Points[i + 1]);
			const float Length = FVector2D::Distance(A, B);
			if (Length < KINDA_SMALL_NUMBER) { continue; }
			const FVector2D Dir = (B - A) / Length;
			float T = bFirst ? 0.f : Step - Carry;
			bFirst = false;
			for (; T <= Length; T += Step) { Disc(A + Dir * T, Radius); }
			Carry = Length - (T - Step);
		}
		if (bLine) { Disc(ToPixels(Points.Last()), Radius); }
	}
	Source.UnlockMip(0, 0, 0);
	FinishTextureEdit(Texture, bSaveAsset);
	FModelingResult R;
	R.bSuccess = true;
	R.AssetPath = Texture->GetPathName();
	R.Message = FString::Printf(TEXT("Drew %s on %s (%lld pixel writes)"), *Kind, *AssetPath, Painted);
	return R;
}

// =========================================================================
// Surface detail helpers
// =========================================================================

FModelingResult UModelingService::AppendMeshAtTransforms(int32 Handle, int32 OtherHandle, const TArray<FTransform>& Transforms)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	UDynamicMesh* Other = FindMesh(OtherHandle);
	if (!Other) { return NoMesh(OtherHandle); }
	if (Handle == OtherHandle) { return Fail(Handle, TEXT("A mesh cannot be stamped into itself - copy_mesh it first")); }
	if (Transforms.Num() == 0) { return Fail(Handle, TEXT("Transforms is empty")); }
	UGeometryScriptDebug* Debug = NewDebug();
	for (const FTransform& T : Transforms)
	{
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(Mesh, Other, T, true, FGeometryScriptAppendMeshOptions(), Debug);
	}
	return Finish(Handle, Mesh, Debug, FString::Printf(TEXT("Stamped handle %d at %d transform(s)"), OtherHandle, Transforms.Num()));
}

FModelingResult UModelingService::AppendMeshAlongPolyline(int32 Handle, int32 OtherHandle, const TArray<FVector>& Points, float Spacing, FVector UpVector, float Scale)
{
	if (Points.Num() < 2) { return Fail(Handle, TEXT("Points needs at least 2 positions")); }
	if (Spacing <= 0.f) { return Fail(Handle, TEXT("Spacing must be positive")); }
	const FVector Up = UpVector.IsNearlyZero() ? FVector::UpVector : UpVector.GetSafeNormal();
	TArray<FTransform> Transforms;
	float Carry = 0.f;
	bool bFirst = true;
	for (int32 i = 0; i + 1 < Points.Num(); ++i)
	{
		const float Length = static_cast<float>(FVector::Distance(Points[i], Points[i + 1]));
		if (Length < KINDA_SMALL_NUMBER) { continue; }
		const FVector Dir = (Points[i + 1] - Points[i]) / Length;
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(Dir, Up).ToQuat();
		float T = bFirst ? 0.f : Spacing - Carry;
		bFirst = false;
		for (; T <= Length + KINDA_SMALL_NUMBER; T += Spacing)
		{
			Transforms.Add(FTransform(Rotation, Points[i] + Dir * T, FVector(Scale)));
		}
		Carry = Length - (T - Spacing);
	}
	if (Transforms.Num() == 0) { return Fail(Handle, TEXT("Polyline is shorter than one Spacing")); }
	FModelingResult R = AppendMeshAtTransforms(Handle, OtherHandle, Transforms);
	if (R.bSuccess) { R.Message = FString::Printf(TEXT("Stamped handle %d %d time(s) along a %d-point polyline every %.1f"), OtherHandle, Transforms.Num(), Points.Num(), Spacing); }
	return R;
}

FModelingResult UModelingService::CutGrooveAlongPolyline(int32 Handle, const TArray<FVector>& Points, float Width, float Depth, FVector UpVector)
{
	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh) { return NoMesh(Handle); }
	if (Points.Num() < 2) { return Fail(Handle, TEXT("Points needs at least 2 positions")); }
	if (Width <= 0.f || Depth <= 0.f) { return Fail(Handle, TEXT("Width and Depth must be positive")); }
	const FVector Up = UpVector.IsNearlyZero() ? FVector::UpVector : UpVector.GetSafeNormal();

	// One frame per point, X along the path (averaged at interior corners), Z along Up; the profile lives in the frame's YZ plane.
	TArray<FTransform> Frames;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		FVector Dir = FVector::ZeroVector;
		if (i > 0) { Dir += (Points[i] - Points[i - 1]).GetSafeNormal(); }
		if (i + 1 < Points.Num()) { Dir += (Points[i + 1] - Points[i]).GetSafeNormal(); }
		if (Dir.IsNearlyZero()) { continue; }
		Frames.Add(FTransform(FRotationMatrix::MakeFromXZ(Dir.GetSafeNormal(), Up).ToQuat(), Points[i]));
	}
	if (Frames.Num() < 2) { return Fail(Handle, TEXT("Polyline has fewer than 2 distinct points")); }
	const TArray<FVector2D> Profile = { FVector2D(-Width * 0.5, -Depth), FVector2D(Width * 0.5, -Depth), FVector2D(Width * 0.5, Depth), FVector2D(-Width * 0.5, Depth) };

	int32 ToolHandle = -1;
	UDynamicMesh* Tool = NewSessionMesh(ToolHandle);
	UGeometryScriptDebug* Debug = NewDebug();
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSweepPolygon(Tool, PrimitiveOptions(0), FTransform::Identity, Profile, Frames, false, true, 1.f, 1.f, 0.f, 1.f, Debug);
	float Area = 0.f, Volume = 0.f;
	UGeometryScriptLibrary_MeshQueryFunctions::GetMeshVolumeArea(Tool, Area, Volume);
	if (Volume < 0.f) { UGeometryScriptLibrary_MeshNormalsFunctions::FlipNormals(Tool, Debug); }
	FModelingResult R = Boolean(Handle, ToolHandle, TEXT("Subtract"), FTransform::Identity, true, true);
	ReleaseMesh(ToolHandle);
	if (R.bSuccess) { R.Message = FString::Printf(TEXT("Cut a %.1f x %.1f groove along a %d-point polyline"), Width, Depth, Points.Num()); }
	return R;
}
