//
// Urho3D Engine
// Copyright (c) 2008-2012 Lasse ��rni
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "CollisionShape.h"
#include "Context.h"
#include "DebugRenderer.h"
#include "Geometry.h"
#include "Log.h"
#include "Model.h"
#include "PhysicsWorld.h"
#include "RigidBody.h"
#include "Scene.h"

#include <hull.h>

#include "DebugNew.h"

void GetVertexAndIndexData(const Model* model, unsigned lodLevel, SharedArrayPtr<Vector3>& destVertexData, unsigned& destVertexCount,
    SharedArrayPtr<unsigned>& destIndexData, unsigned& destIndexCount, const Vector3& scale)
{
    const Vector<Vector<SharedPtr<Geometry> > >& geometries = model->GetGeometries();
    
    destVertexCount = 0;
    destIndexCount = 0;
    
    for (unsigned i = 0; i < geometries.Size(); ++i)
    {
        unsigned subGeometryLodLevel = lodLevel;
        if (subGeometryLodLevel >= geometries[i].Size())
            subGeometryLodLevel = geometries[i].Size() / 2;
        
        Geometry* geom = geometries[i][subGeometryLodLevel];
        if (!geom)
            continue;
        
        destVertexCount += geom->GetVertexCount();
        destIndexCount += geom->GetIndexCount();
    }
    
    if (!destVertexCount || !destIndexCount)
        return;
    
    destVertexData = new Vector3[destVertexCount];
    destIndexData = new unsigned[destIndexCount];
    
    unsigned firstVertex = 0;
    unsigned firstIndex = 0;
    
    for (unsigned i = 0; i < geometries.Size(); ++i)
    {
        unsigned subGeometryLodLevel = lodLevel;
        if (subGeometryLodLevel >= geometries[i].Size())
            subGeometryLodLevel = geometries[i].Size() / 2;
        
        Geometry* geom = geometries[i][subGeometryLodLevel];
        if (!geom)
            continue;
        
        const unsigned char* vertexData;
        const unsigned char* indexData;
        unsigned vertexSize;
        unsigned indexSize;
        
        geom->GetRawData(vertexData, vertexSize, indexData, indexSize);
        if (!vertexData || !indexData)
            continue;
        
        unsigned vertexStart = geom->GetVertexStart();
        unsigned vertexCount = geom->GetVertexCount();
        
        // Copy vertex data
        for (unsigned j = 0; j < vertexCount; ++j)
        {
            const Vector3& v = *((const Vector3*)(&vertexData[(vertexStart + j) * vertexSize]));
            destVertexData[firstVertex + j] = scale * v;
        }
        
        unsigned indexStart = geom->GetIndexStart();
        unsigned indexCount = geom->GetIndexCount();
        
        // 16-bit indices
        if (indexSize == sizeof(unsigned short))
        {
            const unsigned short* indices = (const unsigned short*)indexData;
            
            for (unsigned j = 0; j < indexCount; j += 3)
            {
                // Rebase the indices according to our vertex numbering
                destIndexData[firstIndex + j] = indices[indexStart + j] - vertexStart + firstVertex;
                destIndexData[firstIndex + j + 1] = indices[indexStart + j + 1] - vertexStart + firstVertex;
                destIndexData[firstIndex + j + 2] = indices[indexStart + j + 2] - vertexStart + firstVertex;
            }
        }
        // 32-bit indices
        else
        {
            const unsigned* indices = (const unsigned*)indexData;
            
            for (unsigned j = 0; j < indexCount; j += 3)
            {
                // Rebase the indices according to our vertex numbering
                destIndexData[firstIndex + j] = indices[indexStart + j] - vertexStart + firstVertex;
                destIndexData[firstIndex + j + 1] = indices[indexStart + j + 1] - vertexStart + firstVertex;
                destIndexData[firstIndex + j + 2] = indices[indexStart + j + 2] - vertexStart + firstVertex;
            }
        }
        
        firstVertex += vertexCount;
        firstIndex += indexCount;
    }
}

TriangleMeshData::TriangleMeshData(Model* model, bool makeConvexHull, float thickness, unsigned lodLevel, const Vector3& scale) :
    indexCount_(0)
{
    modelName_ = model->GetName();
    
    unsigned vertexCount;
    unsigned indexCount;
    
    if (!makeConvexHull)
        GetVertexAndIndexData(model, lodLevel, vertexData_, vertexCount, indexData_, indexCount, scale);
    else
    {
        SharedArrayPtr<Vector3> originalVertices;
        SharedArrayPtr<unsigned> originalIndices;
        unsigned originalVertexCount;
        unsigned originalIndexCount;
        
        GetVertexAndIndexData(model, lodLevel, originalVertices, originalVertexCount, originalIndices, originalIndexCount, scale);
        
        // Build the convex hull from the raw geometry
        StanHull::HullDesc desc;
        desc.SetHullFlag(StanHull::QF_TRIANGLES);
        desc.mVcount = originalVertexCount;
        desc.mVertices = (float*)originalVertices.Get();
        desc.mVertexStride = 3 * sizeof(float);
        desc.mSkinWidth = thickness;
        
        StanHull::HullLibrary lib;
        StanHull::HullResult result;
        lib.CreateConvexHull(desc, result);
    
        vertexCount = result.mNumOutputVertices;
        indexCount = result.mNumIndices;
        
        // Copy vertex data
        vertexData_ = new Vector3[vertexCount];
        memcpy(vertexData_.Get(), result.mOutputVertices, vertexCount * sizeof(Vector3));
        
        // Copy index data
        indexData_ = new unsigned[indexCount];
        memcpy(indexData_.Get(), result.mIndices, indexCount * sizeof(unsigned));
        
        lib.ReleaseResult(result);
    }
    
    indexCount_ = indexCount;
}

TriangleMeshData::~TriangleMeshData()
{
}

HeightfieldData::HeightfieldData(Model* model, IntVector2 numPoints, float thickness, unsigned lodLevel,
    const Vector3& scale)
{
    modelName_ = model->GetName();
    
    const Vector<Vector<SharedPtr<Geometry> > >& geometries = model->GetGeometries();
    
    if (!geometries.Size())
        return;
    
    lodLevel = Clamp(lodLevel, 0, geometries[0].Size());
    
    Geometry* geom = geometries[0][lodLevel];
    if (!geom)
        return;
    
    const unsigned char* vertexData;
    const unsigned char* indexData;
    unsigned vertexSize;
    unsigned indexSize;
    
    geom->GetRawData(vertexData, vertexSize, indexData, indexSize);
    if (!vertexData || !indexData)
        return;
    
    unsigned indexStart = geom->GetIndexStart();
    unsigned indexCount = geom->GetIndexCount();
    
    // If X & Z size not specified, try to guess them
    if (numPoints == IntVector2::ZERO)
        numPoints.x_ = numPoints.y_ = (int)sqrtf((float)geom->GetVertexCount());
    unsigned dataSize = numPoints.x_ * numPoints.y_;
    
    // Then allocate the heightfield
    BoundingBox box = model->GetBoundingBox();
    heightData_ = new float[dataSize];
    
    // Calculate spacing from model's bounding box
    float xSpacing = (box.max_.x_ - box.min_.x_) / (numPoints.x_ - 1);
    float zSpacing = (box.max_.z_ - box.min_.z_) / (numPoints.y_ - 1);
    
    // Initialize the heightfield with minimum height
    for (unsigned i = 0; i < dataSize; ++i)
        heightData_[i] = box.min_.y_ * scale.y_;
    
    unsigned vertexStart = geom->GetVertexStart();
    unsigned vertexCount = geom->GetVertexCount();
    
    // Now go through vertex data and fit the vertices into the heightfield
    for (unsigned i = vertexStart; i < vertexStart + vertexCount; ++i)
    {
        const Vector3& vertex = *((const Vector3*)(&vertexData[i * vertexSize]));
        int x = (int)((vertex.x_ - box.min_.x_) / xSpacing + 0.25f);
        int z = (int)((vertex.z_ - box.min_.z_) / zSpacing + 0.25f);
        if (x >= numPoints.x_)
            x = numPoints.x_ - 1;
        if (z >= numPoints.y_)
            z = numPoints.y_ - 1;
        if (vertex.y_ > heightData_[z * numPoints.x_ + x])
            heightData_[z * numPoints.x_ + x] = vertex.y_ * scale.y_;
    }
}

HeightfieldData::~HeightfieldData()
{
}

OBJECTTYPESTATIC(CollisionShape);

CollisionShape::CollisionShape(Context* context) :
    Component(context),
    position_(Vector3::ZERO),
    rotation_(Quaternion::IDENTITY),
    cachedWorldScale_(Vector3::ONE),
    dirty_(false)
{
}

CollisionShape::~CollisionShape()
{
    if (physicsWorld_)
        physicsWorld_->RemoveCollisionShape(this);
}

void CollisionShape::OnSetAttribute(const AttributeInfo& attr, const Variant& src)
{
    Serializable::OnSetAttribute(attr, src);
    dirty_ = true;
}

void CollisionShape::ApplyAttributes()
{
    if (dirty_)
    {
        UpdateCollisionShape();
        NotifyRigidBody();
    }
}

void CollisionShape::SetPosition(const Vector3& position)
{
    position_ = position;
    NotifyRigidBody();
}

void CollisionShape::SetRotation(const Quaternion& rotation)
{
    rotation_ = rotation;
    NotifyRigidBody();
}

void CollisionShape::SetTransform(const Vector3& position, const Quaternion& rotation)
{
    position_ = position;
    rotation_ = rotation;
    NotifyRigidBody();
}

void CollisionShape::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    /// \todo Implement
}

void CollisionShape::OnNodeSet(Node* node)
{
    if (node)
    {
        Scene* scene = node->GetScene();
        if (scene)
        {
            physicsWorld_ = scene->GetComponent<PhysicsWorld>();
            if (physicsWorld_)
                physicsWorld_->AddCollisionShape(this);
        }
        node->AddListener(this);
        UpdateCollisionShape();
        NotifyRigidBody();
    }
}

void CollisionShape::OnMarkedDirty(Node* node)
{
    Vector3 newWorldScale = node_->GetWorldScale();
    if (newWorldScale != cachedWorldScale_)
    {
        UpdateCollisionShape();
        NotifyRigidBody();
        
        cachedWorldScale_ = newWorldScale;
    }
}

void CollisionShape::NotifyRigidBody()
{
    // We need to notify the rigid body also after having been removed from the node, so maintain a weak pointer to it.
    if (!rigidBody_)
        rigidBody_ = GetComponent<RigidBody>();
    
    if (rigidBody_)
        rigidBody_->RefreshCollisionShapes();
    
    dirty_ = false;
}
