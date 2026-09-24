#!/bin/sh
# Smoke test of the Training tab API against a running SeeSharp control server
# (host build with the synthetic scene, or a real CM5). Exercises the whole
# classifier loop (classes -> ROI capture -> train -> activate -> prediction),
# dataset export, model upload and the YOLO labeling endpoints.
#   sh board/host/training_smoke.sh [http://host:8081]
# Training itself runs only when /training/check reports PyTorch; otherwise the
# script verifies capture/export/labeling and skips train/activate.
set -eu
H="${1:-http://127.0.0.1:8081}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
json() { python3 -c "import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))" "$1"; }
post() { curl -sS -m "${3:-20}" -X POST "$H$1" -d "$2"; }
fail() { echo "FAIL: $*" >&2; exit 1; }
wait_capture() {
    i=0
    while [ $i -lt 120 ]; do
        active=$(curl -sS -m 5 "$H/training/status" | json "d['capture']['active']")
        [ "$active" = "False" ] && return 0
        i=$((i + 1)); sleep 0.5
    done
    fail "capture did not finish"
}

echo "== status"
curl -sS -m 5 "$H/training/status" | json "'dataset dir', d['dataset']['dir'], 'mode', d['active']['processing_mode']"
echo "== trainer check"
AVAILABLE=$(post /training/check '{}' 30 | json "d['available']")
echo "trainer available: $AVAILABLE"

echo "== classes"
post /training/classes '{"op":"add","name":"smoke_a"}' >/dev/null
post /training/classes '{"op":"add","name":"smoke_b"}' >/dev/null
post /training/classes '{"op":"add","name":"../bad"}' | grep -q error || fail "bad class name accepted"

echo "== ROI capture (two halves of the frame)"
post /training/capture '{"class":"smoke_a","region_mode":"roi","roi":[0.02,0.40,0.46,0.45],"count":30,"interval_ms":60}' | grep -q started || fail "capture a"
wait_capture
post /training/capture '{"class":"smoke_b","region_mode":"roi","roi":[0.52,0.40,0.46,0.45],"count":30,"interval_ms":60}' | grep -q started || fail "capture b"
wait_capture
curl -sS -m 5 "$H/training/status" | json "[(c['name'], c['count']) for c in d['dataset']['classes'] if c['name'].startswith('smoke_')]"
FILE=$(curl -sS -m 5 "$H/training/samples?class=smoke_a&limit=1" | json "d['items'][0]['file']")
curl -sS -m 5 -o "$TMP/thumb.jpg" -w "thumb: HTTP %{http_code}, %{size_download} bytes\n" "$H/training/thumb?class=smoke_a&file=$FILE"
post /training/samples/delete "{\"class\":\"smoke_a\",\"files\":[\"$FILE\"]}" | grep -q '"deleted":1' || fail "sample delete"

echo "== export classifier zip"
curl -sS -m 60 -o "$TMP/classifier.zip" "$H/training/export?kind=classifier"
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; print('zip entries:', len(z.namelist()))" "$TMP/classifier.zip"

if [ "$AVAILABLE" = "True" ]; then
    echo "== train (8 epochs)"
    post /training/train '{"epochs":8}' | grep -q running || fail "train start"
    i=0
    while [ $i -lt 600 ]; do
        state=$(curl -sS -m 5 "$H/training/status" | json "d['trainer']['state']")
        [ "$state" != "running" ] && break
        i=$((i + 1)); sleep 1
    done
    [ "$state" = "done" ] || { curl -sS -m 5 "$H/training/train/log" | json "'\n'.join(e['line'] for e in d['entries'])"; fail "trainer state $state"; }
    MODEL=$(curl -sS -m 5 "$H/training/status" | json "__import__('os').path.basename(d['trainer']['result']['onnx'])")
    echo "trained: $MODEL"
    echo "== activate + prediction"
    post /training/activate "{\"model\":\"$MODEL\",\"region_mode\":\"roi\"}" | grep -q activated || fail "activate"
    # point the live ROI at the smoke_b half of the frame and read the prediction
    curl -sS -m 5 "$H/config" | python3 -c "import json,sys; c=json.load(sys.stdin); c['classification']['roi']=[0.52,0.40,0.46,0.45]; print(json.dumps(c))" > "$TMP/apply.json"
    curl -sS -m 20 -X POST "$H/apply" --data-binary "@$TMP/apply.json" >/dev/null
    sleep 2
    curl -sS -m 5 "$H/metadata/last" | json "d['detector'], [(o.get('class_name'), round(o['confidence'], 2)) for o in d['objects']]"
    echo "== upload the same model under another name"
    curl -sS -m 60 -X POST "$H/training/model" -H "X-File-Name: smoke_upload.onnx" -H "X-Model-Kind: classifier" \
        -H 'X-Class-Names: ["smoke_a","smoke_b"]' -H "X-Input-Size: 64" \
        --data-binary "@$(curl -sS -m 5 "$H/training/status" | json "d['trainer']['result']['onnx']")" | grep -q saved || fail "upload"
else
    echo "== train skipped (no PyTorch on the device); fallback path is export + host training"
fi

echo "== YOLO labeling"
post /training/yolo/classes '{"classes":["thing","other"]}' | grep -q thing || fail "yolo classes"
post /training/capture '{"kind":"yolo","count":3,"interval_ms":200}' | grep -q started || fail "yolo capture"
wait_capture
IMG=$(curl -sS -m 5 "$H/training/yolo/samples?limit=1" | json "d['items'][0]['file']")
curl -sS -m 5 -o "$TMP/frame.jpg" -w "frame: HTTP %{http_code}, %{size_download} bytes\n" "$H/training/yolo/image?file=$IMG"
post /training/yolo/labels "{\"file\":\"$IMG\",\"boxes\":[{\"class_id\":1,\"cx\":0.5,\"cy\":0.5,\"w\":0.2,\"h\":0.3}]}" | grep -q '"boxes":1' || fail "yolo save labels"
curl -sS -m 5 "$H/training/yolo/labels?file=$IMG" | json "d['boxes']"
curl -sS -m 60 -o "$TMP/yolo.zip" "$H/training/export?kind=yolo"
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; print(z.read('yolo/dataset.yaml').decode())" "$TMP/yolo.zip"
post /training/yolo/delete "{\"files\":[\"$IMG\"]}" | grep -q deleted || fail "yolo delete"

echo "== cleanup"
post /training/classes '{"op":"delete","name":"smoke_a","confirm":true}' >/dev/null
post /training/classes '{"op":"delete","name":"smoke_b","confirm":true}' >/dev/null
echo "SMOKE OK"
