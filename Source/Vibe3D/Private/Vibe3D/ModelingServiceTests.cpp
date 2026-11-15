// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Vibe3D/UModelingService.h"
#include "EditorAssetLibrary.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshBoneWeightFunctions.h"

// Self-provisioning: every test builds its own geometry through the service, writes only under
// kModelingTestDir, and releases / deletes what it made. No project content is required.

static const EAutomationTestFlags kModelingTestFlags =
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
static const TCHAR* kModelingTestDir = TEXT("/Game/Developers/Vibe3DModelingTests");

namespace
{
	struct FScopedModelingSession
	{
		~FScopedModelingSession() { UModelingService::ReleaseAllMeshes(); }
	};

	int32 MakeBox(float Size = 100.f)
	{
		const int32 Handle = UModelingService::CreateMesh().Handle;
		UModelingService::AppendBox(Handle, FTransform::Identity, Size, Size, Size);
		return Handle;
	}

	int32 MakeSphere(float Radius = 50.f)
	{
		const int32 Handle = UModelingService::CreateMesh().Handle;
		UModelingService::AppendSphere(Handle, FTransform::Identity, Radius, 12, 18);
		return Handle;
	}

	/**
	 * Write a minimal binary glTF the way an image-to-3D service delivers one: one node per session mesh (positions in
	 * metres, Y up, a planar UV), one material whose base colour is an embedded TextureSize^2 PNG. No content on disk needed.
	 */
	bool WriteTestGLB(const FString& Path, const TArray<int32>& Handles, int32 TextureSize)
	{
		TArray<uint8> Bin;
		const auto Align = [&Bin]() { while (Bin.Num() % 4) { Bin.Add(0); } };
		const auto Append = [&Bin](const void* Data, int32 Bytes) { Bin.Append(static_cast<const uint8*>(Data), Bytes); };
		TArray<FString> Accessors, Views, Meshes, Nodes;
		const auto AddView = [&](int32 Offset, int32 Length)
		{
			Views.Add(FString::Printf(TEXT("{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d}"), Offset, Length));
			return Views.Num() - 1;
		};
		for (const int32 Handle : Handles)
		{
			TArray<FVector3f> Positions;
			TArray<FVector2f> UVs;
			TArray<uint32> Indices;
			UModelingService::GetDynamicMesh(Handle)->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& M)
			{
				TMap<int32, int32> Remap;
				for (const int32 Vid : M.VertexIndicesItr())
				{
					const FVector P = M.GetVertex(Vid);
					Remap.Add(Vid, Positions.Num());
					Positions.Add(FVector3f(P.X, P.Z, P.Y) / 100.f);   // UE cm Z-up -> glTF m Y-up
					UVs.Add(FVector2f(P.X / 200.f + 0.5f, P.Z / 200.f + 0.5f));
				}
				for (const int32 Tid : M.TriangleIndicesItr())
				{
					const UE::Geometry::FIndex3i T = M.GetTriangle(Tid);
					Indices.Append({ static_cast<uint32>(Remap[T.A]), static_cast<uint32>(Remap[T.C]), static_cast<uint32>(Remap[T.B]) });
				}
			});
			FBox3f Box(Positions);
			Align(); const int32 PosOffset = Bin.Num(); Append(Positions.GetData(), Positions.Num() * sizeof(FVector3f));
			Align(); const int32 UVOffset = Bin.Num(); Append(UVs.GetData(), UVs.Num() * sizeof(FVector2f));
			Align(); const int32 IdxOffset = Bin.Num(); Append(Indices.GetData(), Indices.Num() * sizeof(uint32));
			const int32 PosView = AddView(PosOffset, Positions.Num() * sizeof(FVector3f));
			const int32 UVView = AddView(UVOffset, UVs.Num() * sizeof(FVector2f));
			const int32 IdxView = AddView(IdxOffset, Indices.Num() * sizeof(uint32));
			const int32 First = Accessors.Num();
			Accessors.Add(FString::Printf(TEXT("{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\",\"min\":[%f,%f,%f],\"max\":[%f,%f,%f]}"),
				PosView, Positions.Num(), Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z));
			Accessors.Add(FString::Printf(TEXT("{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"}"), UVView, UVs.Num()));
			Accessors.Add(FString::Printf(TEXT("{\"bufferView\":%d,\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"}"), IdxView, Indices.Num()));
			Meshes.Add(FString::Printf(TEXT("{\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"TEXCOORD_0\":%d},\"indices\":%d,\"material\":0}]}"), First, First + 1, First + 2));
			Nodes.Add(FString::Printf(TEXT("{\"mesh\":%d,\"name\":\"Part%d\"}"), Meshes.Num() - 1, Meshes.Num() - 1));
		}
		TArray<FColor> Pixels;
		for (int32 Y = 0; Y < TextureSize; ++Y)
		{
			for (int32 X = 0; X < TextureSize; ++X) { Pixels.Add(((X / 8 + Y / 8) % 2) ? FColor(200, 60, 40) : FColor(240, 220, 200)); }
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(TextureSize, TextureSize, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		Align(); const int32 PngOffset = Bin.Num(); Append(Png.GetData(), static_cast<int32>(Png.Num()));
		const int32 PngView = AddView(PngOffset, static_cast<int32>(Png.Num()));
		Align();

		TArray<FString> NodeIds;
		for (int32 i = 0; i < Nodes.Num(); ++i) { NodeIds.Add(FString::FromInt(i)); }
		FString Json = FString::Printf(TEXT("{\"asset\":{\"version\":\"2.0\",\"generator\":\"Vibe3D test\"},\"scene\":0,\"scenes\":[{\"nodes\":[%s]}],\"nodes\":[%s],\"meshes\":[%s],")
			TEXT("\"materials\":[{\"name\":\"Generated\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicFactor\":0.0,\"roughnessFactor\":0.8}}],")
			TEXT("\"textures\":[{\"source\":0}],\"images\":[{\"bufferView\":%d,\"mimeType\":\"image/png\",\"name\":\"albedo\"}],")
			TEXT("\"accessors\":[%s],\"bufferViews\":[%s],\"buffers\":[{\"byteLength\":%d}]}"),
			*FString::Join(NodeIds, TEXT(",")), *FString::Join(Nodes, TEXT(",")), *FString::Join(Meshes, TEXT(",")), PngView,
			*FString::Join(Accessors, TEXT(",")), *FString::Join(Views, TEXT(",")), Bin.Num());
		FTCHARToUTF8 Utf8(*Json);
		TArray<uint8> JsonBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		while (JsonBytes.Num() % 4) { JsonBytes.Add(' '); }

		TArray<uint8> Out;
		const auto Put32 = [&Out](uint32 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 4); };
		Put32(0x46546C67); Put32(2); Put32(12 + 8 + JsonBytes.Num() + 8 + Bin.Num());
		Put32(JsonBytes.Num()); Put32(0x4E4F534A); Out.Append(JsonBytes);
		Put32(Bin.Num()); Put32(0x004E4942); Out.Append(Bin);
		return FFileHelper::SaveArrayToFile(Out, *Path);
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingSessionTest, "Vibe3D.Modeling.Session", kModelingTestFlags)
bool FVibe3DModelingSessionTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const FModelingResult Created = UModelingService::CreateMesh();
	TestTrue(TEXT("create succeeds"), Created.bSuccess);
	TestTrue(TEXT("handle is positive"), Created.Handle > 0);

	const FModelingResult Box = UModelingService::AppendBox(Created.Handle, FTransform::Identity, 100.f, 100.f, 100.f);
	TestEqual(TEXT("box has 12 triangles"), Box.TriangleCount, 12);

	const FModelingMeshInfo Info = UModelingService::GetMeshInfo(Created.Handle);
	TestTrue(TEXT("info succeeds"), Info.bSuccess);
	TestEqual(TEXT("info triangle count"), Info.TriangleCount, 12);
	TestEqual(TEXT("info vertex count"), Info.VertexCount, 8);
	TestTrue(TEXT("box is closed"), Info.bIsClosed);
	TestEqual(TEXT("box bounds min z is 0 (Base origin)"), Info.BoundsMin.Z, 0.0);
	TestTrue(TEXT("bounds max z ~100"), FMath::IsNearlyEqual(Info.BoundsMax.Z, 100.0, 0.01));

	const FModelingResult Copy = UModelingService::CopyMesh(Created.Handle);
	TestTrue(TEXT("copy succeeds"), Copy.bSuccess && Copy.Handle != Created.Handle);
	TestEqual(TEXT("copy has same triangles"), Copy.TriangleCount, 12);
	TestEqual(TEXT("two meshes listed"), UModelingService::ListMeshes().Num(), 2);
	TestTrue(TEXT("dynamic mesh accessible"), UModelingService::GetDynamicMesh(Created.Handle) != nullptr);
	TestTrue(TEXT("release copy"), UModelingService::ReleaseMesh(Copy.Handle));
	TestFalse(TEXT("release twice fails"), UModelingService::ReleaseMesh(Copy.Handle));
	TestEqual(TEXT("release all count"), UModelingService::ReleaseAllMeshes(), 1);
	TestEqual(TEXT("nothing listed"), UModelingService::ListMeshes().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingPrimitivesTest, "Vibe3D.Modeling.Primitives", kModelingTestFlags)
bool FVibe3DModelingPrimitivesTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 H = UModelingService::CreateMesh().Handle;
	int32 Last = 0;
	auto Grows = [&](const FModelingResult& R, const TCHAR* What)
	{
		TestTrue(FString::Printf(TEXT("%s succeeds: %s"), What, *R.Message), R.bSuccess);
		TestTrue(FString::Printf(TEXT("%s adds triangles"), What), R.TriangleCount > Last);
		Last = R.TriangleCount;
	};
	Grows(UModelingService::AppendBox(H, FTransform::Identity), TEXT("box"));
	Grows(UModelingService::AppendSphere(H, FTransform(FVector(120, 0, 0))), TEXT("sphere"));
	Grows(UModelingService::AppendSphereBox(H, FTransform(FVector(240, 0, 0))), TEXT("sphere box"));
	Grows(UModelingService::AppendCylinder(H, FTransform(FVector(360, 0, 0))), TEXT("cylinder"));
	Grows(UModelingService::AppendCone(H, FTransform(FVector(480, 0, 0))), TEXT("cone"));
	Grows(UModelingService::AppendCapsule(H, FTransform(FVector(600, 0, 0))), TEXT("capsule"));
	Grows(UModelingService::AppendTorus(H, FTransform(FVector(720, 0, 0))), TEXT("torus"));
	Grows(UModelingService::AppendRectangle(H, FTransform(FVector(0, 120, 0))), TEXT("rectangle"));
	Grows(UModelingService::AppendDisc(H, FTransform(FVector(120, 120, 0)), 50.f, 16, 0, 0.f, 270.f, 10.f), TEXT("disc"));
	Grows(UModelingService::AppendStairs(H, FTransform(FVector(240, 120, 0))), TEXT("stairs"));
	Grows(UModelingService::AppendCurvedStairs(H, FTransform(FVector(0, 300, 0))), TEXT("curved stairs"));
	const TArray<FVector2D> L = { FVector2D(0, 0), FVector2D(40, 0), FVector2D(40, 20), FVector2D(0, 30) };
	Grows(UModelingService::AppendExtrudePolygon(H, FTransform(FVector(480, 120, 0)), L, 15.f), TEXT("extrude polygon"));
	const TArray<FVector2D> Profile = { FVector2D(0, 0), FVector2D(20, 0), FVector2D(25, 40), FVector2D(0, 60) };
	Grows(UModelingService::AppendRevolvePolygon(H, FTransform(FVector(0, 240, 0)), Profile, 0.f, 16, 360.f), TEXT("revolve"));
	const TArray<FVector2D> V = { FVector2D(-4, 0), FVector2D(0, 4), FVector2D(4, 0) };
	const TArray<FTransform> Path = { FTransform(FVector(0, 0, 0)), FTransform(FVector(0, 0, 50)), FTransform(FVector(30, 0, 100)) };
	Grows(UModelingService::AppendSweepPolyline(H, FTransform(FVector(120, 240, 0)), V, Path), TEXT("sweep"));

	const int32 Other = MakeBox(20.f);
	Grows(UModelingService::AppendMesh(H, Other, FTransform(FVector(0, 0, 300))), TEXT("append mesh"));
	TestFalse(TEXT("unknown origin rejected"), UModelingService::AppendBox(H, FTransform::Identity, 1, 1, 1, 0, 0, 0, TEXT("Sideways")).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingBooleanTest, "Vibe3D.Modeling.Booleans", kModelingTestFlags)
bool FVibe3DModelingBooleanTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 A = MakeBox();
	const int32 B = UModelingService::CreateMesh().Handle;
	UModelingService::AppendSphere(B, FTransform(FVector(50, 50, 50)), 40.f, 10, 16);

	const FModelingResult Sub = UModelingService::Boolean(A, B, TEXT("Subtract"), FTransform::Identity);
	TestTrue(TEXT("subtract succeeds"), Sub.bSuccess);
	TestTrue(TEXT("subtract still closed"), UModelingService::GetMeshInfo(A).bIsClosed);
	TestTrue(TEXT("subtract changed topology"), Sub.TriangleCount > 12);

	const int32 C = MakeBox();
	const int32 D = UModelingService::CreateMesh().Handle;
	UModelingService::AppendBox(D, FTransform(FVector(50, 0, 0)), 100.f, 100.f, 100.f);
	TestTrue(TEXT("union"), UModelingService::Boolean(C, D, TEXT("Union"), FTransform::Identity).bSuccess);
	TestTrue(TEXT("intersection"), UModelingService::Boolean(MakeBox(), D, TEXT("Intersection"), FTransform::Identity).bSuccess);
	TestTrue(TEXT("self union"), UModelingService::SelfUnion(C).bSuccess);
	TestTrue(TEXT("plane cut"), UModelingService::PlaneCut(C, FTransform(FVector(0, 0, 80)), true).bSuccess);
	TestEqual(TEXT("plane cut keeps mesh closed"), UModelingService::GetMeshInfo(C).OpenBorderEdges, 0);
	const FModelingMeshInfo Before = UModelingService::GetMeshInfo(C);
	TestTrue(TEXT("mirror"), UModelingService::Mirror(C, FTransform(FRotator(0, 0, 90)), true, true).bSuccess);
	const FModelingMeshInfo After = UModelingService::GetMeshInfo(C);
	TestTrue(TEXT("mirror produced geometry"), After.TriangleCount > 0 && Before.TriangleCount > 0);
	TestFalse(TEXT("unknown op rejected"), UModelingService::Boolean(A, B, TEXT("Frobnicate"), FTransform::Identity).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingSelectionEditTest, "Vibe3D.Modeling.SelectionsPolyEdit", kModelingTestFlags)
bool FVibe3DModelingSelectionEditTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 H = MakeBox();
	TestEqual(TEXT("select all"), UModelingService::SelectAll(H, TEXT("all")), 12);
	TestEqual(TEXT("select top by normal"), UModelingService::SelectByNormalAngle(H, TEXT("top"), FVector::UpVector, 5.f), 2);
	TestTrue(TEXT("select in box"), UModelingService::SelectInBox(H, TEXT("lower"), FVector(-60, -60, -1), FVector(60, 60, 30)) > 0);
	TestTrue(TEXT("select in sphere"), UModelingService::SelectInSphere(H, TEXT("corner"), FVector(50, 50, 100), 150.f) > 0);
	TestEqual(TEXT("selection count"), UModelingService::SelectionCount(H, TEXT("top")), 2);
	FVector Min, Max;
	TestTrue(TEXT("selection bounds"), UModelingService::SelectionBounds(H, TEXT("top"), Min, Max));
	TestTrue(TEXT("top bounds at z=100"), FMath::IsNearlyEqual(Max.Z, 100.0, 0.01) && FMath::IsNearlyEqual(Min.Z, 100.0, 0.01));
	TestTrue(TEXT("expand"), UModelingService::ExpandContractSelection(H, TEXT("top"), TEXT("grown"), 1, false) > 2);
	TestEqual(TEXT("invert"), UModelingService::InvertSelection(H, TEXT("top"), TEXT("not_top")), 10);
	TestTrue(TEXT("polygroups by angle"), UModelingService::ComputePolygroups(H, TEXT("Angle"), 15.f).bSuccess);
	TestTrue(TEXT("select by polygroup"), UModelingService::SelectByPolygroup(H, TEXT("g1"), 1) >= 0);

	TestTrue(TEXT("inset"), UModelingService::InsetFaces(H, TEXT("top"), 6.f).bSuccess);
	TestEqual(TEXT("inset keeps the inner panel selected"), UModelingService::SelectionCount(H, TEXT("top")), 2);
	const FModelingResult Extruded = UModelingService::ExtrudeFaces(H, TEXT("top"), -1.5f);
	TestTrue(TEXT("extrude the inset panel without re-selecting"), Extruded.bSuccess);
	TestTrue(TEXT("only the inner panel recessed"), UModelingService::SelectionBounds(H, TEXT("top"), Min, Max)
		&& FMath::IsNearlyEqual(Max.Z, 98.5, 0.01) && Max.X < 49.0);
	TestTrue(TEXT("outset"), UModelingService::OutsetFaces(H, TEXT("top"), 2.f).bSuccess);
	TestTrue(TEXT("side select"), UModelingService::SelectByNormalAngle(H, TEXT("side"), FVector(1, 0, 0), 5.f) > 0);
	TestTrue(TEXT("offset faces"), UModelingService::OffsetFaces(H, TEXT("side"), 2.f).bSuccess);
	TestTrue(TEXT("side selection follows the offset faces"), UModelingService::TranslateSelection(H, TEXT("side"), FVector(1, 0, 0)).bSuccess);
	UModelingService::SelectByNormalAngle(H, TEXT("side"), FVector(1, 0, 0), 5.f);
	TestTrue(TEXT("translate selection"), UModelingService::TranslateSelection(H, TEXT("side"), FVector(1, 0, 0)).bSuccess);
	TestTrue(TEXT("set material id"), UModelingService::SetMaterialID(H, TEXT("side"), 3).bSuccess);
	TestTrue(TEXT("select by material id"), UModelingService::SelectByMaterialID(H, TEXT("mat3"), 3) > 0);
	TestTrue(TEXT("remap material id"), UModelingService::RemapMaterialID(H, 3, 1).bSuccess);
	TestEqual(TEXT("material 3 gone"), UModelingService::SelectByMaterialID(H, TEXT("mat3b"), 3), 0);
	TestTrue(TEXT("vertex color on selection"), UModelingService::SetVertexColor(H, TEXT("side"), FLinearColor::Green).bSuccess);
	TestTrue(TEXT("constant vertex color"), UModelingService::SetVertexColor(H, TEXT(""), FLinearColor::White).bSuccess);
	TestTrue(TEXT("bevel"), (UModelingService::ComputePolygroups(H, TEXT("Angle"), 15.f), UModelingService::BevelPolygroups(H, 0.5f).bSuccess));
	TestTrue(TEXT("bottom select"), UModelingService::SelectByNormalAngle(H, TEXT("bottom"), FVector::DownVector, 5.f) > 0);
	TestTrue(TEXT("delete faces"), UModelingService::DeleteFaces(H, TEXT("bottom")).bSuccess);
	TestTrue(TEXT("hole after delete"), UModelingService::GetMeshInfo(H).OpenBorderEdges > 0);
	TestTrue(TEXT("fill holes"), UModelingService::FillHoles(H).bSuccess);
	TestEqual(TEXT("closed after fill"), UModelingService::GetMeshInfo(H).OpenBorderEdges, 0);
	TestTrue(TEXT("clear selections"), UModelingService::ClearSelections(H));
	TestFalse(TEXT("missing selection rejected"), UModelingService::InsetFaces(H, TEXT("nope"), 1.f).bSuccess);
	UModelingService::SelectByNormalAngle(H, TEXT("stale"), FVector(1, 0, 0), 5.f);
	UModelingService::Subdivide(H, 1, TEXT("Uniform"));
	TestFalse(TEXT("stale selection rejected after topology change"), UModelingService::TranslateSelection(H, TEXT("stale"), FVector(1, 0, 0)).bSuccess);

	const int32 Flat = UModelingService::CreateMesh().Handle;
	UModelingService::AppendRectangle(Flat, FTransform::Identity, 50.f, 50.f);
	TestTrue(TEXT("shell"), UModelingService::ShellMesh(Flat, 2.f).bSuccess);
	TestTrue(TEXT("shell is closed"), UModelingService::GetMeshInfo(Flat).bIsClosed);
	TestTrue(TEXT("offset mesh"), UModelingService::OffsetMesh(Flat, 1.f).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingMeshOpsTest, "Vibe3D.Modeling.MeshOps", kModelingTestFlags)
bool FVibe3DModelingMeshOpsTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 M = MakeSphere(40.f);
	TestTrue(TEXT("noise"), UModelingService::Noise(M, TEXT(""), 2.f, 0.1f, 3).bSuccess);
	const FModelingResult Remeshed = UModelingService::Remesh(M, 1500);
	TestTrue(TEXT("remesh"), Remeshed.bSuccess && Remeshed.TriangleCount > 1000);
	const FModelingResult Simp = UModelingService::SimplifyToTriangleCount(M, 600);
	TestTrue(TEXT("simplify to tris"), Simp.bSuccess && Simp.TriangleCount <= 620);
	TestTrue(TEXT("simplify to verts"), UModelingService::SimplifyToVertexCount(M, 250).bSuccess);
	TestTrue(TEXT("simplify to tolerance"), UModelingService::SimplifyToTolerance(M, 0.5f).bSuccess);
	for (const TCHAR* Method : { TEXT("PN"), TEXT("Uniform"), TEXT("Loop") })
	{
		const int32 Before = UModelingService::GetMeshInfo(M).TriangleCount;
		const FModelingResult R = UModelingService::Subdivide(M, 1, Method);
		TestTrue(FString::Printf(TEXT("subdivide %s"), Method), R.bSuccess && R.TriangleCount > Before);
		UModelingService::SimplifyToTriangleCount(M, 600);
	}
	const int32 Q = MakeBox(50.f);
	UModelingService::ComputePolygroups(Q, TEXT("Polygons"));
	const FModelingResult CC = UModelingService::Subdivide(Q, 1, TEXT("CatmullClark"));
	TestTrue(TEXT("subdivide CatmullClark"), CC.bSuccess && CC.TriangleCount > 12);
	TestTrue(TEXT("planar simplify"), UModelingService::SimplifyPlanar(MakeBox(), 0.01f).bSuccess);
	TestFalse(TEXT("unknown subdivide rejected"), UModelingService::Subdivide(M, 1, TEXT("Sierpinski")).bSuccess);
	TestTrue(TEXT("smooth"), UModelingService::Smooth(M, TEXT(""), 2, 0.2f).bSuccess);
	TestTrue(TEXT("weld"), UModelingService::WeldEdges(M, 0.001f).bSuccess);
	TestTrue(TEXT("repair"), UModelingService::Repair(M).bSuccess);
	TestTrue(TEXT("remove hidden"), UModelingService::RemoveHiddenTriangles(M).bSuccess);

	const int32 Two = MakeBox();
	UModelingService::AppendBox(Two, FTransform(FVector(500, 0, 0)));
	TestEqual(TEXT("two components"), UModelingService::GetMeshInfo(Two).ConnectedComponents, 2);
	const FModelingSplitResult Split = UModelingService::SplitByComponents(Two);
	TestTrue(TEXT("split succeeds"), Split.bSuccess);
	TestEqual(TEXT("split into two handles"), Split.Handles.Num(), 2);
	TestTrue(TEXT("polygroups UV islands"), UModelingService::ComputePolygroups(M, TEXT("UVIslands")).bSuccess);
	TestTrue(TEXT("polygroups components"), UModelingService::ComputePolygroups(M, TEXT("Components")).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingDeformVoxelTest, "Vibe3D.Modeling.DeformVoxel", kModelingTestFlags)
bool FVibe3DModelingDeformVoxelTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 V = UModelingService::CreateMesh().Handle;
	UModelingService::AppendCylinder(V, FTransform::Identity, 20.f, 100.f, 12, 8);
	const FTransform Mid(FVector(0, 0, 50));
	TestTrue(TEXT("bend"), UModelingService::Bend(V, Mid, 30.f, 50.f).bSuccess);
	TestTrue(TEXT("twist"), UModelingService::Twist(V, Mid, 30.f, 50.f).bSuccess);
	TestTrue(TEXT("flare"), UModelingService::Flare(V, Mid, 20.f, 20.f, 50.f).bSuccess);
	const FModelingResult Solid = UModelingService::VoxelSolidify(V, 32);
	TestTrue(TEXT("voxel solidify"), Solid.bSuccess && Solid.TriangleCount > 0);
	TestTrue(TEXT("solidified is closed"), UModelingService::GetMeshInfo(V).bIsClosed);
	TestTrue(TEXT("voxel close"), UModelingService::VoxelMorphology(V, TEXT("Close"), 2.f, 32).bSuccess);
	TestFalse(TEXT("unknown morphology rejected"), UModelingService::VoxelMorphology(V, TEXT("Wobble"), 1.f, 32).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingAttributesTest, "Vibe3D.Modeling.AttributesTransforms", kModelingTestFlags)
bool FVibe3DModelingAttributesTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 U = MakeBox(60.f);
	TestTrue(TEXT("set uv layers"), UModelingService::SetNumUVLayers(U, 2).bSuccess);
	TestEqual(TEXT("two uv layers"), UModelingService::GetMeshInfo(U).NumUVLayers, 2);
	TestTrue(TEXT("auto uv xatlas"), UModelingService::AutoUV(U, TEXT("XAtlas"), 0).bSuccess);
	TestTrue(TEXT("auto uv patch builder"), UModelingService::AutoUV(U, TEXT("PatchBuilder"), 1).bSuccess);
	TestFalse(TEXT("unknown uv method rejected"), UModelingService::AutoUV(U, TEXT("Magic"), 0).bSuccess);
	for (const TCHAR* Method : { TEXT("Planar"), TEXT("Box"), TEXT("Cylinder") })
	{
		TestTrue(FString::Printf(TEXT("project uv %s"), Method), UModelingService::ProjectUV(U, Method, FTransform::Identity, 0).bSuccess);
	}
	TestTrue(TEXT("repack"), UModelingService::RepackUV(U, 0, 512).bSuccess);
	TestTrue(TEXT("normals hard"), UModelingService::RecomputeNormals(U, 30.f).bSuccess);
	TestTrue(TEXT("normals smooth"), UModelingService::RecomputeNormals(U, -1.f).bSuccess);
	TestTrue(TEXT("flip normals"), UModelingService::FlipNormals(U).bSuccess && UModelingService::FlipNormals(U).bSuccess);

	const FModelingMeshInfo Before = UModelingService::GetMeshInfo(U);
	TestTrue(TEXT("transform"), UModelingService::TransformMesh(U, FTransform(FVector(10, 0, 0))).bSuccess);
	TestTrue(TEXT("translate"), UModelingService::TranslateMesh(U, FVector(-10, 0, 0)).bSuccess);
	TestTrue(TEXT("rotate"), UModelingService::RotateMesh(U, FRotator(0, 45, 0)).bSuccess);
	TestTrue(TEXT("scale"), UModelingService::ScaleMesh(U, FVector(1, 1, 2)).bSuccess);
	TestTrue(TEXT("scaled height doubled"), FMath::IsNearlyEqual(UModelingService::GetMeshInfo(U).BoundsMax.Z, Before.BoundsMax.Z * 2.0, 0.1));
	TestTrue(TEXT("recenter bounds"), UModelingService::RecenterMesh(U, TEXT("Bounds")).bSuccess);
	TestTrue(TEXT("centered"), FMath::IsNearlyZero(UModelingService::GetMeshInfo(U).BoundsMin.Z + UModelingService::GetMeshInfo(U).BoundsMax.Z, 0.1));
	TestTrue(TEXT("recenter base"), UModelingService::RecenterMesh(U, TEXT("Base")).bSuccess);
	TestTrue(TEXT("on the floor"), FMath::IsNearlyZero(UModelingService::GetMeshInfo(U).BoundsMin.Z, 0.01));
	TestFalse(TEXT("unknown recenter rejected"), UModelingService::RecenterMesh(U, TEXT("Sideways")).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingQueriesTest, "Vibe3D.Modeling.QueriesHulls", kModelingTestFlags)
bool FVibe3DModelingQueriesTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const int32 S = MakeSphere(50.f);
	const FModelingSurfacePoint Hit = UModelingService::RayCast(S, FVector(-200, 0, 0), FVector(1, 0, 0));
	TestTrue(TEXT("ray hits"), Hit.bFound);
	TestTrue(TEXT("ray hit near x=-50"), FMath::IsNearlyEqual(Hit.Position.X, -50.0, 2.0));
	TestFalse(TEXT("ray miss"), UModelingService::RayCast(S, FVector(-200, 500, 0), FVector(1, 0, 0)).bFound);
	const FModelingSurfacePoint Near = UModelingService::NearestPoint(S, FVector(0, 0, 120));
	TestTrue(TEXT("nearest found"), Near.bFound && FMath::IsNearlyEqual(Near.Position.Z, 50.0, 2.0));
	TestTrue(TEXT("origin inside"), UModelingService::IsPointInside(S, FVector::ZeroVector));
	TestFalse(TEXT("far point outside"), UModelingService::IsPointInside(S, FVector(0, 0, 500)));
	TestTrue(TEXT("samples"), UModelingService::SampleSurfacePoints(S, 15.f).Num() > 20);

	const FModelingResult Hull = UModelingService::ConvexHull(S);
	TestTrue(TEXT("convex hull"), Hull.bSuccess && Hull.TriangleCount > 0 && Hull.Handle != S);
	const FModelingDistanceReport Report = UModelingService::MeasureDistance(S, Hull.Handle);
	TestTrue(TEXT("distance measured"), Report.bSuccess);
	TestTrue(TEXT("hull hugs sphere"), Report.MaxDistance < 5.f);
	const int32 Boxes = MakeBox();
	UModelingService::AppendBox(Boxes, FTransform(FVector(150, 0, 0)), 50.f, 50.f, 50.f);
	const FModelingResult Decomp = UModelingService::ConvexDecomposition(Boxes, 2);
	TestTrue(TEXT("convex decomposition"), Decomp.bSuccess && Decomp.TriangleCount > 0);
	const FModelingResult Swept = UModelingService::SweptHull(S, FTransform::Identity);
	TestTrue(TEXT("swept hull"), Swept.bSuccess && Swept.TriangleCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingAssetsTest, "Vibe3D.Modeling.Assets", kModelingTestFlags)
bool FVibe3DModelingAssetsTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const FString AssetPath = FString(kModelingTestDir) / TEXT("SM_ModelingTest");
	if (UEditorAssetLibrary::DoesDirectoryExist(kModelingTestDir))
	{
		UEditorAssetLibrary::DeleteDirectory(kModelingTestDir);
	}

	const int32 H = MakeBox();
	UModelingService::AutoUV(H, TEXT("XAtlas"), 0);
	const FModelingResult Saved = UModelingService::SaveMeshToStaticMesh(H, AssetPath, true, true, false, false);
	TestTrue(FString::Printf(TEXT("save creates asset: %s"), *Saved.Message), Saved.bSuccess);
	TestTrue(TEXT("asset exists"), UEditorAssetLibrary::DoesAssetExist(AssetPath));
	TestFalse(TEXT("replace refused when disabled"), UModelingService::SaveMeshToStaticMesh(H, AssetPath, false, true, false, false).bSuccess);
	TestTrue(TEXT("replace LOD0"), UModelingService::SaveMeshToStaticMesh(H, AssetPath, true, true, false, false).bSuccess);

	const FModelingResult Loaded = UModelingService::LoadMeshFromStaticMesh(AssetPath, 0);
	TestTrue(TEXT("load back"), Loaded.bSuccess && Loaded.TriangleCount == 12);
	TestTrue(TEXT("collision"), UModelingService::GenerateCollision(AssetPath, TEXT("ConvexHulls"), 2, 25, false).bSuccess);
	TestFalse(TEXT("unknown collision method rejected"), UModelingService::GenerateCollision(AssetPath, TEXT("Blobs"), 1, 25, false).bSuccess);
	const FModelingResult LODs = UModelingService::SetLODs(AssetPath, { 1.f, 0.5f }, true, false);
	TestTrue(FString::Printf(TEXT("lods: %s"), *LODs.Message), LODs.bSuccess);

	UEditorActorSubsystem* SpawnSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	AActor* Spawned = SpawnSubsystem ? SpawnSubsystem->SpawnActorFromObject(LoadObject<UStaticMesh>(nullptr, *AssetPath), FVector(0, 0, -5000), FRotator::ZeroRotator, false) : nullptr;
	TestNotNull(TEXT("spawn"), Spawned);
	if (Spawned) { Spawned->SetActorLabel(TEXT("Vibe3DModelingTestActor")); }
	const FModelingResult FromActor = UModelingService::LoadMeshFromActor(TEXT("Vibe3DModelingTestActor"), true, 0);
	TestTrue(TEXT("load from actor"), FromActor.bSuccess && FromActor.TriangleCount == 12);
	TestTrue(TEXT("world space applied"), UModelingService::GetMeshInfo(FromActor.Handle).BoundsMin.Z < -4000.0);

	const int32 Source = MakeSphere(60.f);
	const FModelingBakeResult Bake = UModelingService::BakeTextures(H, Source, TEXT("TangentNormal,AmbientOcclusion"), 64, kModelingTestDir, TEXT("ModelingTest"));
	TestTrue(FString::Printf(TEXT("bake: %s"), *Bake.Message), Bake.bSuccess);
	TestEqual(TEXT("two textures"), Bake.TexturePaths.Num(), 2);
	TestFalse(TEXT("unknown bake type rejected"), UModelingService::BakeTextures(H, Source, TEXT("Sparkle"), 64, kModelingTestDir, TEXT("X")).bSuccess);

	// Cleanup: actor, then the whole test folder.
	if (UEditorActorSubsystem* Actors = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr)
	{
		for (AActor* Actor : Actors->GetAllLevelActors())
		{
			if (Actor && Actor->GetActorLabel() == TEXT("Vibe3DModelingTestActor"))
			{
				Actors->DestroyActor(Actor);
			}
		}
	}
	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingErrorsTest, "Vibe3D.Modeling.Errors", kModelingTestFlags)
bool FVibe3DModelingErrorsTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const FModelingResult NoHandle = UModelingService::AppendBox(999, FTransform::Identity);
	TestFalse(TEXT("unknown handle fails"), NoHandle.bSuccess);
	TestTrue(TEXT("unknown handle explains"), NoHandle.Message.Contains(TEXT("create_mesh")));
	const int32 H = MakeBox();
	const FModelingResult BadOp = UModelingService::Boolean(H, H, TEXT("Frobnicate"), FTransform::Identity);
	TestTrue(TEXT("bad enum lists values"), !BadOp.bSuccess && BadOp.Message.Contains(TEXT("Union")));
	TestFalse(TEXT("empty mesh cannot be saved"), UModelingService::SaveMeshToStaticMesh(UModelingService::CreateMesh().Handle, TEXT("/Game/Developers/Nope"), true, true, false, false).bSuccess);
	TestFalse(TEXT("relative asset path rejected"), UModelingService::SaveMeshToStaticMesh(H, TEXT("Nope"), true, true, false, false).bSuccess);
	TestFalse(TEXT("missing texture rejected"), UModelingService::DisplaceFromTexture(H, TEXT(""), TEXT("/Game/Developers/DoesNotExist"), 1.f).bSuccess);
	TestFalse(TEXT("missing skeleton rejected"), UModelingService::SmoothBoneWeights(H, TEXT("/Game/Developers/NoSkeleton")).bSuccess);
	TestFalse(TEXT("empty bone list rejected"), UModelingService::PruneBoneWeights(H, TEXT(" , ")).bSuccess);
	TestEqual(TEXT("selection on unknown handle"), UModelingService::SelectAll(4242, TEXT("x")), -1);
	TestFalse(TEXT("ray cast on unknown handle"), UModelingService::RayCast(4242, FVector::ZeroVector, FVector::UpVector).bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingRiggingTest, "Vibe3D.Modeling.LoftsRigging", kModelingTestFlags)
bool FVibe3DModelingRiggingTest::RunTest(const FString&)
{
	FScopedModelingSession Session;

	// Loft: a tapered wing section must come out closed and outward-facing whatever the sweep's winding.
	const int32 Wing = UModelingService::CreateMesh().Handle;
	const TArray<FVector2D> Profile = { FVector2D(0, 0), FVector2D(0.3, 0.06), FVector2D(0.7, 0.04), FVector2D(1, 0), FVector2D(0.7, -0.04), FVector2D(0.3, -0.06) };
	const TArray<FTransform> Frames = {
		FTransform(FRotator(0, 90, 0), FVector(0, 0, 0), FVector(200)),
		FTransform(FRotator(0, 90, 0), FVector(-50, 400, 0), FVector(100)) };
	const FModelingResult Loft = UModelingService::AppendLoft(Wing, FTransform::Identity, Profile, Frames);
	TestTrue(FString::Printf(TEXT("loft: %s"), *Loft.Message), Loft.bSuccess);
	const FModelingMeshInfo WingInfo = UModelingService::GetMeshInfo(Wing);
	TestTrue(TEXT("loft is closed"), WingInfo.bIsClosed);
	TestTrue(TEXT("loft faces outward"), WingInfo.Volume > 0.f);
	TestTrue(TEXT("loft spans the frames"), WingInfo.BoundsMax.Y > 390.0 && WingInfo.BoundsMin.X < -150.0);
	UModelingService::FlipNormals(Wing);
	TestTrue(TEXT("flipped volume is negative"), UModelingService::GetMeshInfo(Wing).Volume < 0.f);
	TestTrue(TEXT("ensure_outward flips"), UModelingService::EnsureOutward(Wing).Message.Contains(TEXT("Flipped")));
	TestTrue(TEXT("volume positive again"), UModelingService::GetMeshInfo(Wing).Volume > 0.f);
	TestTrue(TEXT("ensure_outward is idempotent"), UModelingService::EnsureOutward(Wing).Message.Contains(TEXT("Already")));
	TestFalse(TEXT("open profile rejected"), UModelingService::AppendLoft(Wing, FTransform::Identity, { FVector2D(0, 0), FVector2D(1, 0) }, Frames).bSuccess);

	// Rig: a body box and a separate flap box, the flap bound to its own bone.
	const int32 H = MakeBox(100.f);
	UModelingService::AppendBox(H, FTransform(FVector(0, 200, 0)), 50.f, 50.f, 50.f);
	TestEqual(TEXT("two components"), UModelingService::GetMeshInfo(H).ConnectedComponents, 2);
	TestEqual(TEXT("select_connected grabs the flap"), UModelingService::SelectConnected(H, TEXT("flap"), FVector(0, 200, 25)), 12);
	TestEqual(TEXT("select_connected grabs the body"), UModelingService::SelectConnected(H, TEXT("body"), FVector(0, 0, 50)), 12);

	FModelingBoneDef Root;
	Root.Name = TEXT("root");
	FModelingBoneDef Flap;
	Flap.Name = TEXT("flap");
	Flap.ParentName = TEXT("root");
	Flap.Transform = FTransform(FVector(0, 175, 0));
	const FModelingResult Rig = UModelingService::CreateBones(H, { Root, Flap });
	TestTrue(FString::Printf(TEXT("create_bones: %s"), *Rig.Message), Rig.bSuccess);
	TestTrue(TEXT("bind flap"), UModelingService::BindSelectionToBone(H, TEXT("flap"), TEXT("flap"), 1.f).bSuccess);
	const TArray<FModelingBoneInfo> Bones = UModelingService::ListBones(H);
	TestEqual(TEXT("two bones"), Bones.Num(), 2);
	if (Bones.Num() == 2)
	{
		TestEqual(TEXT("root influences the body"), Bones[0].InfluencedVertices, 8);
		TestEqual(TEXT("flap influences the flap"), Bones[1].InfluencedVertices, 8);
		TestEqual(TEXT("flap parent"), Bones[1].ParentName, FString(TEXT("root")));
		TestTrue(TEXT("flap mesh-space pose"), Bones[1].MeshTransform.GetLocation().Equals(FVector(0, 175, 0), 0.01));
	}
	TestFalse(TEXT("unknown bone rejected"), UModelingService::BindSelectionToBone(H, TEXT("flap"), TEXT("nope")).bSuccess);
	TestFalse(TEXT("child before parent rejected"), UModelingService::CreateBones(Wing, { Flap, Root }).bSuccess);
	TestFalse(TEXT("stale selection rejected"), UModelingService::BindSelectionToBone(Wing, TEXT("missing"), TEXT("root")).bSuccess);

	// Save as a skeletal mesh with a freshly created skeleton, assign materials, then clean up.
	const FString MeshPath = FString(kModelingTestDir) + TEXT("/SK_ModelingRig");
	const FModelingResult Saved = UModelingService::SaveMeshToSkeletalMesh(H, MeshPath, TEXT(""), true, false);
	TestTrue(FString::Printf(TEXT("save skeletal: %s"), *Saved.Message), Saved.bSuccess);
	TestTrue(TEXT("skeletal mesh asset exists"), UEditorAssetLibrary::DoesAssetExist(MeshPath));
	TestTrue(TEXT("skeleton asset created"), UEditorAssetLibrary::DoesAssetExist(MeshPath + TEXT("_Skeleton")));
	if (USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(MeshPath + TEXT("_Skeleton"))))
	{
		TestEqual(TEXT("skeleton has both bones"), Skeleton->GetReferenceSkeleton().GetRawBoneNum(), 2);
		TestEqual(TEXT("flap bone in skeleton"), Skeleton->GetReferenceSkeleton().FindRawBoneIndex(TEXT("flap")), 1);
	}
	TestTrue(TEXT("set materials"), UModelingService::SetAssetMaterials(MeshPath, TEXT("/Engine/BasicShapes/BasicShapeMaterial"), false).bSuccess);
	TestFalse(TEXT("missing material rejected"), UModelingService::SetAssetMaterials(MeshPath, TEXT("/Game/Developers/NoSuchMaterial"), false).bSuccess);
	TestFalse(TEXT("skeletal save without weights rejected"), UModelingService::SaveMeshToSkeletalMesh(MakeBox(), FString(kModelingTestDir) + TEXT("/SK_NoWeights"), TEXT(""), true, false).bSuccess);
	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingUVsTexturingTest, "Vibe3D.Modeling.UVsTexturing", kModelingTestFlags)
bool FVibe3DModelingUVsTexturingTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const FString Dir = kModelingTestDir;

	// --- UV control on a two-material box: one polygroup per face, conformal unwrap, pack, verify
	const int32 H = MakeBox(100.f);
	UModelingService::SelectByNormalAngle(H, TEXT("top"), FVector::UpVector, 5.f);
	UModelingService::SetMaterialID(H, TEXT("top"), 1);
	TestTrue(TEXT("polygroups by angle"), UModelingService::ComputePolygroups(H, TEXT("Angle"), 15.f, 1).bSuccess);
	const FModelingResult Group = UModelingService::SetPolygroup(H, TEXT("top"), -1);
	TestTrue(FString::Printf(TEXT("set_polygroup: %s"), *Group.Message), Group.bSuccess);
	const FModelingResult Unwrap = UModelingService::RecomputeUVs(H, 0, TEXT("Polygroups"), TEXT("SpectralConformal"));
	TestTrue(FString::Printf(TEXT("recompute_uvs: %s"), *Unwrap.Message), Unwrap.bSuccess);
	TestTrue(TEXT("repack"), UModelingService::LayoutUV(H, 0, TEXT("Repack"), TEXT(""), 512).bSuccess);
	FModelingUVStats Stats = UModelingService::GetUVStats(H, 0, 256);
	TestTrue(FString::Printf(TEXT("uv stats: %s"), *Stats.Message), Stats.bSuccess);
	TestEqual(TEXT("one island per face"), Stats.NumIslands, 6);
	TestEqual(TEXT("no unset triangles"), Stats.NumUnsetTriangles, 0);
	TestTrue(TEXT("packed islands do not overlap"), Stats.OverlapFraction < 0.01f);
	TestTrue(TEXT("packed islands cover the square"), Stats.Coverage > 0.05f);
	TestTrue(TEXT("texel density reported"), Stats.TexelsPerCmAt1K > 0.f);
	TestTrue(TEXT("transform_uv"), UModelingService::TransformUV(H, 0, TEXT("top"), FVector2D(0.05, 0), FVector2D(1, 1), 0.f).bSuccess);
	TestTrue(TEXT("pack per material"), UModelingService::PackUVPerMaterial(H, 0, 512).bSuccess);
	Stats = UModelingService::GetUVStats(H, 0, 256);
	TestTrue(TEXT("per-material packs overlap each other by design"), Stats.OverlapFraction > 0.f);
	FVector2D UV;
	TestTrue(TEXT("world_to_uv finds the top face"), UModelingService::WorldToUV(H, FVector(0, 0, 100), 0, UV));
	TestTrue(TEXT("uv inside the square"), UV.X >= -0.01 && UV.X <= 1.01 && UV.Y >= -0.01 && UV.Y <= 1.01);
	TestFalse(TEXT("bad island source rejected"), UModelingService::RecomputeUVs(H, 0, TEXT("Vibes")).bSuccess);
	TestFalse(TEXT("bad layout type rejected"), UModelingService::LayoutUV(H, 0, TEXT("Sideways")).bSuccess);
	TestFalse(TEXT("missing uv layer rejected"), UModelingService::TransformUV(H, 7, TEXT(""), FVector2D::ZeroVector, FVector2D(1, 1)).bSuccess);

	// --- Bakes: masks from vertex colours / material ids, the UV shell wireframe, height
	UModelingService::SetVertexColor(H, TEXT(""), FLinearColor::Red);
	const FModelingBakeResult Masks = UModelingService::BakeTextures(H, H, TEXT("VertexColor,MaterialID,UVShell,Height"), 64, Dir, TEXT("UVTest"));
	TestTrue(FString::Printf(TEXT("mask bakes: %s"), *Masks.Message), Masks.bSuccess);
	TestEqual(TEXT("four mask textures"), Masks.TexturePaths.Num(), 4);

	// --- Image tools: noise, drawing, texture transfer (image import is Epic's TextureToolset.ImportFile)
	const FString NoisePath = Dir + TEXT("/T_Noise");
	const FModelingResult Noise = UModelingService::CreateNoiseTexture(NoisePath, 64, 64, 4.f, 3, 0.5f, 7, false);
	TestTrue(FString::Printf(TEXT("noise: %s"), *Noise.Message), Noise.bSuccess);
	TestTrue(TEXT("noise asset exists"), UEditorAssetLibrary::DoesAssetExist(NoisePath));
	TestTrue(TEXT("draw line"), UModelingService::DrawOnTexture(NoisePath, TEXT("Line"), { FVector2D(0.1, 0.1), FVector2D(0.9, 0.9) }, FLinearColor::White, 3.f, 8.f, false).bSuccess);
	TestTrue(TEXT("draw dots"), UModelingService::DrawOnTexture(NoisePath, TEXT("Dots"), { FVector2D(0.1, 0.9), FVector2D(0.9, 0.1) }, FLinearColor::Black, 2.f, 6.f, false).bSuccess);
	TestTrue(TEXT("draw rect"), UModelingService::DrawOnTexture(NoisePath, TEXT("Rect"), { FVector2D(0.2, 0.2), FVector2D(0.4, 0.4) }, FLinearColor::Gray, 1.f, 1.f, false).bSuccess);
	TestFalse(TEXT("unknown shape rejected"), UModelingService::DrawOnTexture(NoisePath, TEXT("Spiral"), {}, FLinearColor::White).bSuccess);
	TestFalse(TEXT("draw on missing texture rejected"), UModelingService::DrawOnTexture(Dir + TEXT("/T_Nope"), TEXT("Fill"), {}, FLinearColor::White).bSuccess);

	const FModelingBakeResult Transfer = UModelingService::BakeTextureTransfer(H, H, NoisePath, 64, Dir, TEXT("Xfer"));
	TestTrue(FString::Printf(TEXT("texture transfer: %s"), *Transfer.Message), Transfer.bSuccess);
	if (TestTrue(TEXT("mask bake produced a texture for the multi-transfer source"), Masks.TexturePaths.Num() > 0))
	{
		const FModelingBakeResult MultiTransfer = UModelingService::BakeTextureTransfer(H, H, NoisePath + TEXT(",") + Masks.TexturePaths[0], 64, Dir, TEXT("Xfer2"));
		TestTrue(FString::Printf(TEXT("multi-texture transfer: %s"), *MultiTransfer.Message), MultiTransfer.bSuccess);
	}
	TestFalse(TEXT("missing source texture rejected"), UModelingService::BakeTextureTransfer(H, H, Dir + TEXT("/T_Nope"), 64, Dir, TEXT("X")).bSuccess);

	// --- Surface detail: stamps and grooves
	const int32 Rivet = MakeSphere(2.f);
	const int32 RivetTris = UModelingService::GetMeshInfo(Rivet).TriangleCount;
	const int32 Before = UModelingService::GetMeshInfo(H).TriangleCount;
	TestTrue(TEXT("stamp at transforms"), UModelingService::AppendMeshAtTransforms(H, Rivet, { FTransform(FVector(-30, 0, 100)), FTransform(FVector(0, 0, 100)), FTransform(FVector(30, 0, 100)) }).bSuccess);
	TestEqual(TEXT("three stamps added"), UModelingService::GetMeshInfo(H).TriangleCount, Before + 3 * RivetTris);
	const FModelingResult Row = UModelingService::AppendMeshAlongPolyline(H, Rivet, { FVector(-40, 20, 100), FVector(40, 20, 100) }, 20.f);
	TestTrue(FString::Printf(TEXT("stamp along polyline: %s"), *Row.Message), Row.bSuccess);
	TestEqual(TEXT("five stamps along 80 cm at 20 cm"), UModelingService::GetMeshInfo(H).TriangleCount, Before + 8 * RivetTris);
	TestFalse(TEXT("self stamp rejected"), UModelingService::AppendMeshAtTransforms(H, H, { FTransform::Identity }).bSuccess);
	const int32 Slab = MakeBox(100.f);
	const int32 SlabBefore = UModelingService::GetMeshInfo(Slab).TriangleCount;
	const FModelingResult Groove = UModelingService::CutGrooveAlongPolyline(Slab, { FVector(-60, -30, 100), FVector(0, -30, 100), FVector(60, 0, 100) }, 2.f, 1.f);
	TestTrue(FString::Printf(TEXT("groove: %s"), *Groove.Message), Groove.bSuccess);
	const FModelingMeshInfo SlabInfo = UModelingService::GetMeshInfo(Slab);
	TestTrue(TEXT("groove added geometry"), SlabInfo.TriangleCount > SlabBefore);
	TestTrue(TEXT("grooved slab still closed"), SlabInfo.bIsClosed);
	TestFalse(TEXT("groove needs two points"), UModelingService::CutGrooveAlongPolyline(Slab, { FVector::ZeroVector }, 2.f, 1.f).bSuccess);

	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

// ---------------------------------------------------------------------------------------------
// Regressions found while building the showcase props (2026-08-30).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingMaterialIdRegressionTest, "Vibe3D.Modeling.MaterialIdOnAppend", kModelingTestFlags)
bool FVibe3DModelingMaterialIdRegressionTest::RunTest(const FString&)
{
	FScopedModelingSession Session;

	// AppendTorus / AppendRevolvePolygon / AppendLinearStairs ignore
	// FGeometryScriptPrimitiveOptions::MaterialID, so the service stamps it on afterwards. Without
	// that a lamp shade or a mug handle silently lands on material slot 0 and cannot be given its
	// own material.
	auto OnlyMaterial = [this](int32 Handle, int32 Expected, const TCHAR* What)
	{
		const TArray<int32> Ids = UModelingService::GetMeshInfo(Handle).MaterialIds;
		TestEqual(FString::Printf(TEXT("%s uses exactly one material id"), What), Ids.Num(), 1);
		if (Ids.Num() == 1)
		{
			TestEqual(FString::Printf(TEXT("%s honours material_id"), What), Ids[0], Expected);
		}
	};

	const int32 Torus = UModelingService::CreateMesh().Handle;
	UModelingService::AppendTorus(Torus, FTransform::Identity, 40.f, 8.f, 16, 10, TEXT("Center"), 7);
	OnlyMaterial(Torus, 7, TEXT("append_torus"));

	const int32 Revolve = UModelingService::CreateMesh().Handle;
	UModelingService::AppendRevolvePolygon(Revolve, FTransform::Identity,
		{ FVector2D(0, 0), FVector2D(20, 0), FVector2D(20, 40), FVector2D(0, 40) }, 0.f, 16, 360.f, 5);
	OnlyMaterial(Revolve, 5, TEXT("append_revolve_polygon"));

	const int32 Stairs = UModelingService::CreateMesh().Handle;
	UModelingService::AppendStairs(Stairs, FTransform::Identity, 40.f, 10.f, 20.f, 4, false, 3);
	OnlyMaterial(Stairs, 3, TEXT("append_stairs"));

	// Stamping must only touch the triangles the append created, not the whole mesh.
	const int32 Mixed = MakeBox(50.f);
	UModelingService::AppendTorus(Mixed, FTransform(FVector(0, 0, 120)), 30.f, 6.f, 16, 10, TEXT("Center"), 2);
	TArray<int32> MixedIds = UModelingService::GetMeshInfo(Mixed).MaterialIds;
	MixedIds.Sort();
	TestEqual(TEXT("box keeps id 0 while the torus takes id 2"), MixedIds, TArray<int32>({ 0, 2 }));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingRepairClosureTest, "Vibe3D.Modeling.RepairKeepsClosed", kModelingTestFlags)
bool FVibe3DModelingRepairClosureTest::RunTest(const FString&)
{
	FScopedModelingSession Session;

	// repair() collapses degenerate triangles, which can open a watertight mesh and then silently
	// break the next boolean (it needs closed input). A mesh that arrives closed must leave closed.
	const int32 Handle = UModelingService::CreateMesh().Handle;
	UModelingService::AppendRevolvePolygon(Handle, FTransform::Identity,
		{ FVector2D(0, 0), FVector2D(36, 0), FVector2D(40, 40), FVector2D(47, 95), FVector2D(0, 95) }, 0.f, 48, 360.f, 0);
	const int32 Cavity = UModelingService::CreateMesh().Handle;
	UModelingService::AppendRevolvePolygon(Cavity, FTransform::Identity,
		{ FVector2D(0, 8), FVector2D(33, 8), FVector2D(37, 40), FVector2D(41, 105), FVector2D(0, 105) }, 0.f, 48, 360.f, 0);
	UModelingService::Boolean(Handle, Cavity, TEXT("Subtract"), FTransform::Identity);

	TestTrue(TEXT("hollowed mesh starts closed"), UModelingService::GetMeshInfo(Handle).bIsClosed);
	const FModelingResult Repaired = UModelingService::Repair(Handle);
	TestTrue(FString::Printf(TEXT("repair succeeded: %s"), *Repaired.Message), Repaired.bSuccess);
	const FModelingMeshInfo After = UModelingService::GetMeshInfo(Handle);
	TestTrue(TEXT("repair left the mesh closed"), After.bIsClosed);
	TestEqual(TEXT("no open border edges after repair"), After.OpenBorderEdges, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingVertexArraysTest, "Vibe3D.Modeling.VertexArrays", kModelingTestFlags)
bool FVibe3DModelingVertexArraysTest::RunTest(const FString&)
{
	FScopedModelingSession Session;

	// Round trip: read, move, write back; the layout is indexed by vertex ID.
	const int32 H = MakeBox(100.f);
	TArray<FVector> Positions = UModelingService::GetVertexPositions(H);
	TestEqual(TEXT("one entry per vertex"), Positions.Num(), 8);
	for (FVector& P : Positions) { P.Z += 10.0; }
	const FModelingResult Set = UModelingService::SetVertexPositions(H, Positions);
	TestTrue(FString::Printf(TEXT("set succeeds: %s"), *Set.Message), Set.bSuccess);
	TestTrue(TEXT("bounds moved up"), FMath::IsNearlyEqual(UModelingService::GetMeshInfo(H).BoundsMin.Z, 10.0, 0.001));
	TestFalse(TEXT("wrong length rejected"), UModelingService::SetVertexPositions(H, { FVector::ZeroVector }).bSuccess);
	TestEqual(TEXT("unknown handle gives an empty list"), UModelingService::GetVertexPositions(4242).Num(), 0);

	// A mesh with ID gaps keeps its slots: the list is MaxVertexID long and gap slots are ignored on write.
	const int32 G = MakeBox(100.f);
	UModelingService::AppendBox(G, FTransform(FVector(300, 0, 0)), 50.f, 50.f, 50.f);
	UModelingService::SelectConnected(G, TEXT("first"), FVector(0, 0, 50));
	UModelingService::DeleteFaces(G, TEXT("first"));
	const FModelingMeshInfo GapInfo = UModelingService::GetMeshInfo(G);
	TArray<FVector> GapPositions = UModelingService::GetVertexPositions(G);
	TestTrue(TEXT("gap layout is at least as long as the vertex count"), GapPositions.Num() >= GapInfo.VertexCount);
	for (FVector& P : GapPositions) { P.X -= 300.0; }
	TestTrue(TEXT("gap layout accepted"), UModelingService::SetVertexPositions(G, GapPositions).bSuccess);
	TestTrue(TEXT("remaining box moved to the origin"), FMath::IsNearlyEqual(UModelingService::GetMeshInfo(G).BoundsMin.X, -25.0, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingIntakeTest, "Vibe3D.Modeling.IntakeGeneratedMesh", kModelingTestFlags)
bool FVibe3DModelingIntakeTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const FString Folder = FString(kModelingTestDir) / TEXT("Intake");
	const FString AssetPath = Folder / TEXT("SM_TestProp");
	if (UEditorAssetLibrary::DoesDirectoryExist(kModelingTestDir)) { UEditorAssetLibrary::DeleteDirectory(kModelingTestDir); }

	// A generated "prop": a sphere and a separate tall box, two meshes in one GLB, a 64 px texture. 150 cm tall as delivered.
	const int32 Sphere = MakeSphere(50.f);
	const int32 Box = UModelingService::CreateMesh().Handle;
	UModelingService::AppendBox(Box, FTransform(FVector(100, 0, 0)), 40.f, 40.f, 100.f, 2, 2, 6);
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Vibe3DTests/intake_test.glb"));
	TestTrue(TEXT("test GLB written"), WriteTestGLB(File, { Sphere, Box }, 64));

	const FModelingIntakeReport First = UModelingService::IntakeGeneratedMesh(File, AssetPath, 75.f, TEXT("Height"), 0.f, TEXT("BottomCenter"),
		200, 32, TEXT("ConvexHulls"), 4, TEXT("1,0.5"));
	TestTrue(FString::Printf(TEXT("intake succeeds: %s"), *First.Message), First.bSuccess);
	AddInfo(First.Message);
	TestTrue(TEXT("asset exists"), UEditorAssetLibrary::DoesAssetExist(AssetPath));
	TestTrue(TEXT("both meshes read"), First.SourceMeshCount >= 1 && First.SourceTriangles > 200);
	TestTrue(FString::Printf(TEXT("decimated to the budget (%d)"), First.TriangleCount), First.TriangleCount > 0 && First.TriangleCount <= 210);
	TestTrue(FString::Printf(TEXT("fitted to 75 cm tall (%s..%s)"), *First.BoundsMin.ToString(), *First.BoundsMax.ToString()),
		FMath::IsNearlyEqual(First.BoundsMax.Z - First.BoundsMin.Z, 75.0, 1.0));
	TestTrue(TEXT("pivot at the floor"), FMath::IsNearlyEqual(First.BoundsMin.Z, 0.0, 0.5));
	TestTrue(TEXT("pivot centred in XY"), FMath::Abs(First.BoundsMin.X + First.BoundsMax.X) < 1.0 && FMath::Abs(First.BoundsMin.Y + First.BoundsMax.Y) < 1.0);
	TestTrue(TEXT("uniform scale"), FMath::IsNearlyEqual(First.ScaleApplied.X, First.ScaleApplied.Z, 1e-4));
	TestEqual(TEXT("two LODs"), First.LODCount, 2);
	TestTrue(TEXT("collision built"), First.CollisionShapes >= 1);
	TestEqual(TEXT("one material slot"), First.MaterialPaths.Num(), 1);
	if (First.MaterialPaths.Num() == 1) { TestTrue(FString::Printf(TEXT("material moved next to the mesh (%s)"), *First.MaterialPaths[0]), First.MaterialPaths[0].StartsWith(Folder / TEXT("M"))); }
	TestEqual(TEXT("one texture"), First.Textures.Num(), 1);
	if (First.Textures.Num() == 1) { TestTrue(FString::Printf(TEXT("texture capped (%s)"), *First.Textures[0]), First.Textures[0].EndsWith(TEXT("max 32")) && First.Textures[0].StartsWith(Folder / TEXT("T_TestProp_"))); }
	TestFalse(TEXT("staging folder removed"), UEditorAssetLibrary::DoesDirectoryExist(Folder / TEXT("_Intake_TestProp")));
	int32 StaticMeshes = 0;
	for (const FString& Path : UEditorAssetLibrary::ListAssets(Folder, true, false))
	{
		if (Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(Path))) { ++StaticMeshes; }
	}
	TestEqual(TEXT("exactly one static mesh kept"), StaticMeshes, 1);
	UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, *AssetPath);

	// Same file and settings: nothing to do.
	const FModelingIntakeReport Again = UModelingService::IntakeGeneratedMesh(File, AssetPath, 75.f, TEXT("Height"), 0.f, TEXT("BottomCenter"),
		200, 32, TEXT("ConvexHulls"), 4, TEXT("1,0.5"));
	TestTrue(FString::Printf(TEXT("second run is up to date: %s"), *Again.Message), Again.bSuccess && Again.bUpToDate);

	// New settings replace the asset in place (same object, so placed actors keep it) without piling up materials.
	const FModelingIntakeReport Boxed = UModelingService::IntakeGeneratedMesh(File, AssetPath, 0.f, TEXT("Box"), 90.f, TEXT("Center"),
		0, 0, TEXT("Box"), 1, TEXT(""), FVector(60, 30, 90));
	TestTrue(FString::Printf(TEXT("box fit succeeds: %s"), *Boxed.Message), Boxed.bSuccess && !Boxed.bUpToDate);
	AddInfo(Boxed.Message);
	TestTrue(TEXT("replaced in place"), LoadObject<UStaticMesh>(nullptr, *AssetPath) == Asset);
	const FVector BoxSize = Boxed.BoundsMax - Boxed.BoundsMin;
	TestTrue(FString::Printf(TEXT("stretched to the box (%s)"), *BoxSize.ToString()), BoxSize.Equals(FVector(60, 30, 90), 1.0));
	TestTrue(TEXT("centre pivot"), (Boxed.BoundsMax + Boxed.BoundsMin).IsNearlyZero(1.0));
	TestEqual(TEXT("LODs cleared back to one"), Boxed.LODCount, 1);
	TestFalse(TEXT("no duplicate material"), UEditorAssetLibrary::DoesAssetExist(Folder / TEXT("MI_TestProp_0_2")) || UEditorAssetLibrary::DoesAssetExist(Folder / TEXT("M_TestProp_0_2")));

	// Somebody else's assets are not intake's to replace: a mesh at the target path needs force, and unreferenced assets
	// that happen to have the names intake wants (no intake tag) are kept, the new ones get a numbered name.
	const FString OtherFolder = FString(kModelingTestDir) / TEXT("IntakeOther");
	const FString OtherPath = OtherFolder / TEXT("SM_TestProp");
	TestTrue(TEXT("a hand-made mesh at the target path"), UModelingService::SaveMeshToStaticMesh(MakeBox(), OtherPath, true, true, false, false).bSuccess);
	TArray<FString> Decoys;
	for (const FString& Made : First.MaterialPaths) { Decoys.Add(OtherFolder / FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(Made))); }
	for (const FString& Made : First.Textures) { Decoys.Add(OtherFolder / FPackageName::GetShortName(FPackageName::ObjectPathToPackageName(Made.Left(Made.Find(TEXT(" ")))))); }
	for (const FString& Decoy : Decoys) { TestTrue(FString::Printf(TEXT("decoy %s"), *Decoy), UModelingService::SaveMeshToStaticMesh(MakeBox(), Decoy, true, false, false, false).bSuccess); }
	const FModelingIntakeReport Refused = UModelingService::IntakeGeneratedMesh(File, OtherPath, 75.f);
	TestTrue(FString::Printf(TEXT("foreign mesh not replaced without force: %s"), *Refused.Message), !Refused.bSuccess && Refused.Message.Contains(TEXT("force")));
	TestTrue(TEXT("foreign mesh untouched"), UModelingService::LoadMeshFromStaticMesh(OtherPath).TriangleCount == 12);
	const FModelingIntakeReport Forced = UModelingService::IntakeGeneratedMesh(File, OtherPath, 75.f, TEXT("Height"), 0.f, TEXT("BottomCenter"),
		200, 32, TEXT("None"), 1, TEXT(""), FVector::ZeroVector, false, true);
	TestTrue(FString::Printf(TEXT("force replaces it: %s"), *Forced.Message), Forced.bSuccess && !Forced.bUpToDate);
	for (const FString& Decoy : Decoys) { TestNotNull(*FString::Printf(TEXT("untagged %s kept"), *Decoy), Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(Decoy))); }
	for (const FString& Made : Forced.MaterialPaths) { TestFalse(FString::Printf(TEXT("new material got its own name (%s)"), *Made), Decoys.Contains(FPackageName::ObjectPathToPackageName(Made))); }
	TestTrue(TEXT("once intake's, re-running needs no force"), UModelingService::IntakeGeneratedMesh(File, OtherPath, 60.f).bSuccess);

	// Refusals.
	TestFalse(TEXT("missing file rejected"), UModelingService::IntakeGeneratedMesh(File + TEXT(".missing"), AssetPath).bSuccess);
	TestFalse(TEXT("unknown fit rejected"), UModelingService::IntakeGeneratedMesh(File, AssetPath, 50.f, TEXT("Diagonal")).bSuccess);
	TestFalse(TEXT("box fit without a box rejected"), UModelingService::IntakeGeneratedMesh(File, AssetPath, 50.f, TEXT("Box")).bSuccess);
	TestFalse(TEXT("bad LOD list rejected"), UModelingService::IntakeGeneratedMesh(File, AssetPath, 50.f, TEXT("Height"), 0.f, TEXT("BottomCenter"), 0, 0, TEXT("None"), 1, TEXT("1,2")).bSuccess);

	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	IFileManager::Get().Delete(*File);
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

namespace
{
	TArray<UMaterialInterface*> LODMaterials(USkeletalMesh* SkeletalMesh, int32 LODIndex)
	{
		TArray<UMaterialInterface*> Materials;
		TArray<int32> Indices;
		TArray<FName> Names;
		FGeometryScriptMeshReadLOD ReadLOD;
		ReadLOD.LODIndex = LODIndex;
		EGeometryScriptOutcomePins Outcome;
		UGeometryScriptLibrary_StaticMeshFunctions::GetLODMaterialListFromSkeletalMesh(SkeletalMesh, ReadLOD, Materials, Indices, Names, Outcome);
		return Materials;
	}

	/** A two-bone, two-material skeletal mesh asset: a subdivided body box (root, material 0) and a flap box (flap, material 1). */
	int32 MakeRiggedTwoMaterialMesh()
	{
		const int32 H = UModelingService::CreateMesh().Handle;
		UModelingService::AppendBox(H, FTransform::Identity, 100.f, 100.f, 100.f, 4, 4, 4);
		UModelingService::AppendBox(H, FTransform(FVector(0, 200, 0)), 50.f, 50.f, 50.f, 2, 2, 2, TEXT("Base"), 1);
		UModelingService::SelectConnected(H, TEXT("flap"), FVector(0, 200, 25));
		FModelingBoneDef Root;
		Root.Name = TEXT("root");
		FModelingBoneDef Flap;
		Flap.Name = TEXT("flap");
		Flap.ParentName = TEXT("root");
		Flap.Transform = FTransform(FVector(0, 175, 0));
		UModelingService::CreateBones(H, { Root, Flap });
		UModelingService::BindSelectionToBone(H, TEXT("flap"), TEXT("flap"), 1.f);
		return H;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingSkeletalLODTest, "Vibe3D.Modeling.SkeletalLODRoundTrip", kModelingTestFlags)
bool FVibe3DModelingSkeletalLODTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	if (UEditorAssetLibrary::DoesDirectoryExist(kModelingTestDir)) { UEditorAssetLibrary::DeleteDirectory(kModelingTestDir); }
	const FString MeshPath = FString(kModelingTestDir) / TEXT("SK_LODRoundTrip");

	const int32 H = MakeRiggedTwoMaterialMesh();
	TestTrue(TEXT("asset saved"), UModelingService::SaveMeshToSkeletalMesh(H, MeshPath, TEXT(""), true, false).bSuccess);
	TestTrue(TEXT("two slots assigned"), UModelingService::SetAssetMaterials(MeshPath,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial,/Engine/EngineMaterials/WorldGridMaterial"), false).bSuccess);
	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!TestNotNull(TEXT("skeletal mesh"), SkeletalMesh)) { return false; }
	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();

	// Append LOD1 from a simplified copy.
	const int32 Low = UModelingService::CopyMesh(H).Handle;
	UModelingService::SimplifyToTriangleCount(Low, UModelingService::GetMeshInfo(H).TriangleCount / 2);
	const FModelingResult Added = UModelingService::SaveMeshToSkeletalMeshLOD(Low, MeshPath, 1, false);
	TestTrue(FString::Printf(TEXT("LOD1 appended: %s"), *Added.Message), Added.bSuccess);
	TestEqual(TEXT("two LODs"), SkeletalMesh->GetLODNum(), 2);
	const FModelingResult Inspected = UModelingService::InspectSkeletalMesh(MeshPath);
	TestTrue(FString::Printf(TEXT("inspect lists both LODs: %s"), *Inspected.Message), Inspected.bSuccess && Inspected.Message.Contains(TEXT("LOD 1:")));
	AddInfo(Inspected.Message);
	const TArray<UMaterialInterface*> LOD0Before = LODMaterials(SkeletalMesh, 0);
	const TArray<UMaterialInterface*> LOD1Before = LODMaterials(SkeletalMesh, 1);
	const int32 LOD0Verts = UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 0).VertexCount;

	// Round trip LOD1 in place: load, move up 10 cm, save with no path.
	const FModelingResult Loaded = UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 1);
	TestTrue(FString::Printf(TEXT("load LOD1: %s"), *Loaded.Message), Loaded.bSuccess && Loaded.Message.Contains(TEXT("source model")));
	UModelingService::TranslateMesh(Loaded.Handle, FVector(0, 0, 10));
	const FModelingResult Saved = UModelingService::SaveMeshToSkeletalMeshLOD(Loaded.Handle);
	TestTrue(FString::Printf(TEXT("LOD1 written back: %s"), *Saved.Message), Saved.bSuccess);
	TestTrue(TEXT("unchanged topology keeps the vertex order"), Saved.Message.Contains(TEXT("original vertex order")));
	AddInfo(Saved.Message);
	TestEqual(TEXT("still two LODs"), SkeletalMesh->GetLODNum(), 2);
	TestTrue(TEXT("same skeleton"), SkeletalMesh->GetSkeleton() == Skeleton);
	TestEqual(TEXT("still two bones"), SkeletalMesh->GetRefSkeleton().GetRawBoneNum(), 2);
	TestEqual(TEXT("still two slots"), SkeletalMesh->GetMaterials().Num(), 2);
	TestTrue(TEXT("LOD1 sections keep their materials"), LODMaterials(SkeletalMesh, 1) == LOD1Before);
	const FModelingResult LOD1After = UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 1);
	TestTrue(TEXT("LOD1 moved up"), FMath::IsNearlyEqual(UModelingService::GetMeshInfo(LOD1After.Handle).BoundsMin.Z, 10.0, 0.01));
	TestEqual(TEXT("LOD1 vertex count kept"), LOD1After.VertexCount, Loaded.VertexCount);
	const FModelingResult LOD0After = UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 0);
	TestTrue(TEXT("LOD0 untouched"), FMath::IsNearlyEqual(UModelingService::GetMeshInfo(LOD0After.Handle).BoundsMin.Z, 0.0, 0.01) && LOD0After.VertexCount == LOD0Verts);

	// LOD0 round trip through the old entry point keeps the slots too.
	UModelingService::TranslateMesh(LOD0After.Handle, FVector(5, 0, 0));
	TestTrue(TEXT("LOD0 replaced via save_mesh_to_skeletal_mesh"), UModelingService::SaveMeshToSkeletalMesh(LOD0After.Handle, MeshPath, TEXT(""), true, false).bSuccess);
	TestTrue(TEXT("LOD0 sections keep their materials"), LODMaterials(SkeletalMesh, 0) == LOD0Before);
	TestEqual(TEXT("LOD1 survives a LOD0 write"), SkeletalMesh->GetLODNum(), 2);

	// Refusals: nothing is written.
	TestFalse(TEXT("no weights rejected"), UModelingService::SaveMeshToSkeletalMeshLOD(MakeBox(), MeshPath, 1, false).bSuccess);
	const int32 Foreign = MakeBox();
	FModelingBoneDef Root;
	Root.Name = TEXT("root");
	FModelingBoneDef Bogus;
	Bogus.Name = TEXT("bogus");
	Bogus.ParentName = TEXT("root");
	UModelingService::CreateBones(Foreign, { Root, Bogus });
	UModelingService::BindSelectionToBone(Foreign, TEXT(""), TEXT("bogus"), 1.f);
	const FModelingResult ForeignResult = UModelingService::SaveMeshToSkeletalMeshLOD(Foreign, MeshPath, 1, false);
	TestTrue(FString::Printf(TEXT("foreign bone rejected: %s"), *ForeignResult.Message), !ForeignResult.bSuccess && ForeignResult.Message.Contains(TEXT("bogus")));
	const int32 BadSlot = UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 1).Handle;
	UModelingService::SetMaterialID(BadSlot, TEXT(""), 7);
	const FModelingResult SlotResult = UModelingService::SaveMeshToSkeletalMeshLOD(BadSlot);
	TestTrue(FString::Printf(TEXT("material without a slot rejected: %s"), *SlotResult.Message), !SlotResult.bSuccess && SlotResult.Message.Contains(TEXT("Material ID 7")));
	const int32 Unweighted = UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 1).Handle;
	bool bValidVertex = false;
	UGeometryScriptLibrary_MeshBoneWeightFunctions::SetVertexBoneWeights(UModelingService::GetDynamicMesh(Unweighted), 0, {}, bValidVertex);
	const FModelingResult UnweightedResult = UModelingService::SaveMeshToSkeletalMeshLOD(Unweighted);
	TestTrue(FString::Printf(TEXT("unweighted vertex rejected: %s"), *UnweightedResult.Message), !UnweightedResult.bSuccess && UnweightedResult.Message.Contains(TEXT("no bone weight")));
	TestFalse(TEXT("LOD beyond the end rejected"), UModelingService::SaveMeshToSkeletalMeshLOD(BadSlot, MeshPath, 5, false).bSuccess);
	TestFalse(TEXT("missing LOD read rejected"), UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 9).bSuccess);
	TestFalse(TEXT("unknown origin needs a path"), UModelingService::SaveMeshToSkeletalMeshLOD(MakeBox()).bSuccess);
	TestEqual(TEXT("refusals left two LODs"), SkeletalMesh->GetLODNum(), 2);

	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingAccessoryReskinTest, "Vibe3D.Modeling.AccessoryReskin", kModelingTestFlags)
bool FVibe3DModelingAccessoryReskinTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	if (UEditorAssetLibrary::DoesDirectoryExist(kModelingTestDir)) { UEditorAssetLibrary::DeleteDirectory(kModelingTestDir); }
	const FString BodyPath = FString(kModelingTestDir) / TEXT("SK_ReskinBody");
	TestTrue(TEXT("body saved"), UModelingService::SaveMeshToSkeletalMesh(MakeRiggedTwoMaterialMesh(), BodyPath, TEXT(""), true, false).bSuccess);
	const int32 Body = UModelingService::LoadMeshFromSkeletalMesh(BodyPath, 0).Handle;

	const auto BoneVertices = [](int32 Handle, const TCHAR* Bone)
	{
		for (const FModelingBoneInfo& Info : UModelingService::ListBones(Handle)) { if (Info.Name == Bone) { return Info.InfluencedVertices; } }
		return -1;
	};

	// A plate lying on the flap follows the flap.
	const int32 Plate = UModelingService::CreateMesh().Handle;
	UModelingService::AppendBox(Plate, FTransform(FVector(0, 200, 50)), 20.f, 20.f, 2.f);
	const FModelingResult Surface = UModelingService::SkinToSurface(Plate, Body);
	TestTrue(FString::Printf(TEXT("surface skin: %s"), *Surface.Message), Surface.bSuccess);
	TestEqual(TEXT("plate carries the body's two bones"), UModelingService::ListBones(Plate).Num(), 2);
	TestEqual(TEXT("every plate vertex on the flap"), BoneVertices(Plate, TEXT("flap")), 8);
	TestEqual(TEXT("none on the root"), BoneVertices(Plate, TEXT("root")), 0);

	// Rigid: one bone for the whole piece, chosen from the surface or named.
	const int32 Badge = UModelingService::CreateMesh().Handle;
	UModelingService::AppendBox(Badge, FTransform(FVector(0, 0, 100)), 20.f, 20.f, 2.f);
	const FModelingResult Rigid = UModelingService::SkinToSurface(Badge, Body, TEXT("Rigid"));
	TestTrue(FString::Printf(TEXT("rigid picks the bone underneath: %s"), *Rigid.Message), Rigid.bSuccess && Rigid.Message.Contains(TEXT("'root'")));
	TestEqual(TEXT("rigid binds every vertex"), BoneVertices(Badge, TEXT("root")), 8);
	TestTrue(TEXT("rigid to a named bone"), UModelingService::SkinToSurface(Badge, Body, TEXT("Rigid"), TEXT("flap")).bSuccess);
	TestEqual(TEXT("named bone took it all"), BoneVertices(Badge, TEXT("flap")), 8);
	TestFalse(TEXT("unknown rigid bone rejected"), UModelingService::SkinToSurface(Badge, Body, TEXT("Rigid"), TEXT("nope")).bSuccess);
	TestTrue(TEXT("inpaint mode"), UModelingService::SkinToSurface(Badge, Body, TEXT("Inpaint")).bSuccess);
	TestTrue(TEXT("bones from an asset"), UModelingService::SkinToSurface(Plate, Body, TEXT("Surface"), TEXT(""), BodyPath).bSuccess);
	TestFalse(TEXT("unskinned source rejected"), UModelingService::SkinToSurface(Plate, MakeBox()).bSuccess);
	TestFalse(TEXT("unknown mode rejected"), UModelingService::SkinToSurface(Plate, Body, TEXT("Glue")).bSuccess);

	// The skinned plate saves as a follower mesh on the body's skeleton.
	USkeletalMesh* BodyAsset = LoadObject<USkeletalMesh>(nullptr, *BodyPath);
	const FString PlatePath = FString(kModelingTestDir) / TEXT("SKM_Plate");
	const FModelingResult PlateSaved = UModelingService::SaveMeshToSkeletalMesh(Plate, PlatePath, BodyAsset->GetSkeleton()->GetPathName(), true, false);
	TestTrue(FString::Printf(TEXT("plate saved on the body skeleton: %s"), *PlateSaved.Message), PlateSaved.bSuccess);

	// LOD bone remap: the reference (leader) drops flap_tip at LOD1; a follower weighted to flap_tip must move to flap.
	const auto MakeThreeBoneMesh = [](const TCHAR* FlapBone)
	{
		const int32 H = UModelingService::CreateMesh().Handle;
		UModelingService::AppendBox(H, FTransform::Identity, 100.f, 100.f, 100.f);
		UModelingService::AppendBox(H, FTransform(FVector(0, 200, 0)), 50.f, 50.f, 50.f);
		UModelingService::SelectConnected(H, TEXT("flap"), FVector(0, 200, 25));
		FModelingBoneDef Root; Root.Name = TEXT("root");
		FModelingBoneDef Flap; Flap.Name = TEXT("flap"); Flap.ParentName = TEXT("root"); Flap.Transform = FTransform(FVector(0, 175, 0));
		FModelingBoneDef Tip; Tip.Name = TEXT("flap_tip"); Tip.ParentName = TEXT("flap"); Tip.Transform = FTransform(FVector(0, 225, 0));
		UModelingService::CreateBones(H, { Root, Flap, Tip });
		UModelingService::BindSelectionToBone(H, TEXT("flap"), FlapBone, 1.f);
		return H;
	};
	const FString RefPath = FString(kModelingTestDir) / TEXT("SK_Leader");
	const int32 Leader = MakeThreeBoneMesh(TEXT("flap"));
	TestTrue(TEXT("leader saved"), UModelingService::SaveMeshToSkeletalMesh(Leader, RefPath, TEXT(""), true, false).bSuccess);
	TestTrue(TEXT("leader LOD1 added"), UModelingService::SaveMeshToSkeletalMeshLOD(UModelingService::CopyMesh(Leader).Handle, RefPath, 1, false).bSuccess);
	USkeletalMesh* LeaderAsset = LoadObject<USkeletalMesh>(nullptr, *RefPath);
	LeaderAsset->Modify();
	FBoneReference Removed;
	Removed.BoneName = TEXT("flap_tip");
	LeaderAsset->GetLODInfo(1)->BonesToRemove.Add(Removed);
	LeaderAsset->PostEditChange();
	FAssetCompilingManager::Get().FinishAllCompilation();
	const FModelingResult LeaderInfo = UModelingService::InspectSkeletalMesh(RefPath);
	AddInfo(LeaderInfo.Message);
	TestTrue(TEXT("the leader drops a bone at LOD1"), LeaderInfo.Message.Contains(TEXT("LOD 1: source model")) && LeaderInfo.Message.Contains(TEXT("2 bone(s) used at runtime")));

	const FString FollowerPath = FString(kModelingTestDir) / TEXT("SKM_Follower");
	TestTrue(TEXT("follower saved"), UModelingService::SaveMeshToSkeletalMesh(MakeThreeBoneMesh(TEXT("flap_tip")), FollowerPath, TEXT(""), true, false).bSuccess);
	const FModelingResult Remap = UModelingService::RemapLODBones(FollowerPath, RefPath, false);
	TestTrue(FString::Printf(TEXT("remap: %s"), *Remap.Message), Remap.bSuccess && Remap.VertexCount == 8 && Remap.Message.Contains(TEXT("flap_tip->flap")));
	AddInfo(Remap.Message);
	const int32 Remapped = UModelingService::LoadMeshFromSkeletalMesh(FollowerPath, 0).Handle;
	TestEqual(TEXT("nothing left on the dropped bone"), BoneVertices(Remapped, TEXT("flap_tip")), 0);
	TestEqual(TEXT("its parent took over"), BoneVertices(Remapped, TEXT("flap")), 8);
	TestEqual(TEXT("second remap is a no-op"), UModelingService::RemapLODBones(FollowerPath, RefPath, false).VertexCount, 0);
	TestFalse(TEXT("missing reference rejected"), UModelingService::RemapLODBones(FollowerPath, TEXT("/Game/Developers/NoSuchMesh"), false).bSuccess);

	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingPosedComponentTest, "Vibe3D.Modeling.PosedComponent", kModelingTestFlags)
bool FVibe3DModelingPosedComponentTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	if (UEditorAssetLibrary::DoesDirectoryExist(kModelingTestDir)) { UEditorAssetLibrary::DeleteDirectory(kModelingTestDir); }
	const FString MeshPath = FString(kModelingTestDir) / TEXT("SK_PoseTest");
	TestTrue(TEXT("asset saved"), UModelingService::SaveMeshToSkeletalMesh(MakeRiggedTwoMaterialMesh(), MeshPath, TEXT(""), true, false).bSuccess);
	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("editor world"), World) || !SkeletalMesh) { return false; }

	// A placed character stand-in: a poseable "body" with the flap bone raised 30 cm, and a leader-pose follower.
	AActor* Actor = World->SpawnActor<AActor>(FVector(1000, 0, 0), FRotator::ZeroRotator);
	UPoseableMeshComponent* Pose = NewObject<UPoseableMeshComponent>(Actor, TEXT("Pose"));
	Actor->SetRootComponent(Pose);
	Pose->SetSkinnedAssetAndUpdate(SkeletalMesh);
	Pose->RegisterComponent();
	USkeletalMeshComponent* Follower = NewObject<USkeletalMeshComponent>(Actor, TEXT("Follower"));
	Follower->SetupAttachment(Pose);
	Follower->SetSkinnedAssetAndUpdate(SkeletalMesh);
	Follower->RegisterComponent();
	Follower->SetLeaderPoseComponent(Pose);
	Actor->SetActorLocation(FVector(1000, 0, 0));
	Actor->SetActorLabel(TEXT("Vibe3DPoseTest"));
	Pose->SetBoneLocationByName(TEXT("flap"), FVector(0, 175, 30), EBoneSpaces::ComponentSpace);
	Pose->RefreshBoneTransforms();

	const auto PieceMinZ = [](int32 Handle, const FVector& Point)
	{
		UModelingService::SelectConnected(Handle, TEXT("piece"), Point);
		FVector Min, Max;
		UModelingService::SelectionBounds(Handle, TEXT("piece"), Min, Max);
		return Min.Z;
	};

	const FModelingResult Posed = UModelingService::LoadMeshFromSkinnedComponent(TEXT("Vibe3DPoseTest"), TEXT("Pose"), false, 0, true);
	TestTrue(FString::Printf(TEXT("posed read: %s"), *Posed.Message), Posed.bSuccess);
	AddInfo(Posed.Message);
	TestTrue(TEXT("flap raised in the pose"), FMath::IsNearlyEqual(PieceMinZ(Posed.Handle, FVector(0, 200, 55)), 30.0, 0.05));
	TestTrue(TEXT("body stays put"), FMath::IsNearlyEqual(PieceMinZ(Posed.Handle, FVector(0, 0, 50)), 0.0, 0.05));
	const FModelingResult Refused = UModelingService::SaveMeshToSkeletalMeshLOD(Posed.Handle);
	TestTrue(FString::Printf(TEXT("posed handle cannot be saved as a reference pose: %s"), *Refused.Message), !Refused.bSuccess && Refused.Message.Contains(TEXT("POSED")));
	TestTrue(TEXT("a copy of a posed handle stays posed"), !UModelingService::SaveMeshToSkeletalMeshLOD(UModelingService::CopyMesh(Posed.Handle).Handle).bSuccess);

	const FModelingResult InWorld = UModelingService::LoadMeshFromSkinnedComponent(TEXT("Vibe3DPoseTest"), TEXT("Pose"), true, 0, true);
	TestTrue(TEXT("world space adds the actor transform"), FMath::IsNearlyEqual(UModelingService::GetMeshInfo(InWorld.Handle).BoundsMin.X, 950.0, 0.05));
	const FModelingResult FromFollower = UModelingService::LoadMeshFromSkinnedComponent(TEXT("Vibe3DPoseTest"), TEXT("Follower"), false, 0, true);
	TestTrue(FString::Printf(TEXT("follower read: %s"), *FromFollower.Message), FromFollower.bSuccess && FromFollower.Message.Contains(TEXT("leader")));
	TestTrue(TEXT("follower takes the leader's pose"), FMath::IsNearlyEqual(PieceMinZ(FromFollower.Handle, FVector(0, 200, 55)), 30.0, 0.05));

	// Edit in the pose, unpose, write: the edit lands in the reference pose.
	UModelingService::SelectConnected(Posed.Handle, TEXT("flap"), FVector(0, 200, 55));
	UModelingService::TranslateSelection(Posed.Handle, TEXT("flap"), FVector(0, 0, 5));
	const FModelingResult Unposed = UModelingService::UnposeMesh(Posed.Handle);
	TestTrue(FString::Printf(TEXT("unpose: %s"), *Unposed.Message), Unposed.bSuccess);
	TestTrue(TEXT("flap back in the reference pose, keeping the 5 cm edit"), FMath::IsNearlyEqual(PieceMinZ(Posed.Handle, FVector(0, 200, 25)), 5.0, 0.05));
	TestTrue(TEXT("body back where it was"), FMath::IsNearlyEqual(PieceMinZ(Posed.Handle, FVector(0, 0, 50)), 0.0, 0.05));
	const FModelingResult Written = UModelingService::SaveMeshToSkeletalMeshLOD(Posed.Handle, TEXT(""), -1, false);
	TestTrue(FString::Printf(TEXT("unposed handle writes back: %s"), *Written.Message), Written.bSuccess);
	TestTrue(TEXT("asset holds the edit"), FMath::IsNearlyEqual(PieceMinZ(UModelingService::LoadMeshFromSkeletalMesh(MeshPath, 0).Handle, FVector(0, 200, 25)), 5.0, 0.05));

	TestFalse(TEXT("unknown actor rejected"), UModelingService::LoadMeshFromSkinnedComponent(TEXT("NoSuchActor")).bSuccess);
	const FModelingResult NoComponent = UModelingService::LoadMeshFromSkinnedComponent(TEXT("Vibe3DPoseTest"), TEXT("Hat"));
	TestTrue(TEXT("unknown component lists the real ones"), !NoComponent.bSuccess && NoComponent.Message.Contains(TEXT("Follower")));
	TestFalse(TEXT("unpose of an unposed handle rejected"), UModelingService::UnposeMesh(Posed.Handle).bSuccess);

	Actor->Destroy();
	FAssetCompilingManager::Get().FinishAllCompilation();
	UModelingService::ReleaseAllMeshes();
	TestTrue(TEXT("test folder removed"), UEditorAssetLibrary::DeleteDirectory(kModelingTestDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibe3DModelingGarmentFitTest, "Vibe3D.Modeling.GarmentFit", kModelingTestFlags)
bool FVibe3DModelingGarmentFitTest::RunTest(const FString&)
{
	FScopedModelingSession Session;
	const auto Sphere = [](float Radius, bool bOpenTop)
	{
		const int32 H = UModelingService::CreateMesh().Handle;
		UModelingService::AppendSphere(H, FTransform::Identity, Radius, 24, 32);
		if (bOpenTop) { UModelingService::PlaneCut(H, FTransform(FVector(0, 0, 30)), false); }   // a "shirt": no cover above z = 30
		return H;
	};
	const auto TopZ = [](int32 Handle)
	{
		double Top = -1e9;
		for (const FVector& P : UModelingService::GetVertexPositions(Handle)) { Top = FMath::Max(Top, P.Z); }
		return Top;
	};

	// Skin (r 50) pokes through a shirt (r 49, open above z 30).
	const int32 Body = Sphere(50.f, false);
	const int32 Shirt = Sphere(49.f, true);
	const FModelingPenetrationReport Before = UModelingService::MeasurePenetration(Body, Shirt, TEXT("Inside"), 0.3f, 5.f, TEXT(""), TEXT("poking"));
	TestTrue(FString::Printf(TEXT("penetration measured: %s"), *Before.Message), Before.bSuccess && Before.Violations > 0 && Before.MaxDepth > 1.0f && Before.MaxDepth < 3.0f);
	AddInfo(Before.Message);
	TestEqual(TEXT("offenders stored as a selection"), UModelingService::SelectionCount(Body, TEXT("poking")), Before.Violations);
	const double HeadTop = TopZ(Body);
	const FModelingResult Pushed = UModelingService::PushInside(Body, Shirt, 0.3f, TEXT(""), 5.f, 2);
	TestTrue(FString::Printf(TEXT("push inside: %s"), *Pushed.Message), Pushed.bSuccess);
	AddInfo(Pushed.Message);
	const FModelingPenetrationReport After = UModelingService::MeasurePenetration(Body, Shirt, TEXT("Inside"), 0.3f);
	TestTrue(FString::Printf(TEXT("skin under the shirt now: %s"), *After.Message), After.bSuccess && After.Violations == 0);
	TestTrue(TEXT("uncovered head left alone"), FMath::IsNearlyEqual(TopZ(Body), HeadTop, 1e-4));

	// A garment (r 48) sunk into the skin (r 50) is pushed out over it.
	const int32 Skin = Sphere(50.f, false);
	const int32 Sunk = Sphere(48.f, true);
	TestTrue(TEXT("sunk garment detected"), UModelingService::MeasurePenetration(Sunk, Skin, TEXT("Outside"), 0.3f).Violations > 0);
	TestTrue(TEXT("push outside"), UModelingService::PushOutside(Sunk, Skin, 0.3f, TEXT(""), 5.f, 2).bSuccess);
	const FModelingPenetrationReport Over = UModelingService::MeasurePenetration(Sunk, Skin, TEXT("Outside"), 0.3f);
	TestTrue(FString::Printf(TEXT("garment over the skin now: %s"), *Over.Message), Over.Violations == 0);

	// Skin well inside a garment can be dropped; the uncovered part stays.
	const int32 Inner = Sphere(45.f, false);
	const int32 Total = UModelingService::GetMeshInfo(Inner).TriangleCount;
	const int32 Hidden = UModelingService::SelectHiddenBy(Inner, TEXT("hidden"), Shirt, 0.5f, 10.f, true);
	TestTrue(FString::Printf(TEXT("hidden triangles found (%d of %d)"), Hidden, Total), Hidden > Total / 4 && Hidden < Total);
	TestTrue(TEXT("hidden faces deleted"), UModelingService::DeleteFaces(Inner, TEXT("hidden")).bSuccess && UModelingService::GetMeshInfo(Inner).TriangleCount == Total - Hidden);

	TestFalse(TEXT("unknown side rejected"), UModelingService::MeasurePenetration(Body, Shirt, TEXT("Above")).bSuccess);
	TestFalse(TEXT("self fit rejected"), UModelingService::PushInside(Body, Body).bSuccess);
	TestEqual(TEXT("hidden-by on a missing handle"), UModelingService::SelectHiddenBy(Body, TEXT("x"), 4242), -1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
