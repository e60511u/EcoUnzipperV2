$oldFolder = 'c:\Users\batyo\Downloads\New folder\Old folder'
$destBase = 'c:\Users\batyo\Downloads\New folder\Content to transfer'
$pageFolder = Join-Path $destBase 'page'
$blogFolder = Join-Path $destBase 'blog article'

if (!(Test-Path $pageFolder)) { New-Item -ItemType Directory -Force -Path $pageFolder }
if (!(Test-Path $blogFolder)) { New-Item -ItemType Directory -Force -Path $blogFolder }

$htmlFiles = Get-ChildItem -Path $oldFolder -Filter *.html

foreach ($file in $htmlFiles) {
    $content = Get-Content $file.FullName -Raw
    
    $dest = $pageFolder
    if ($content -match 'class="[^"]*single-post[^"]*"') {
        $dest = $blogFolder
    } elseif ($content -match 'class="[^"]*page-template-default[^"]*"') {
        $dest = $pageFolder
    } elseif ($content -match 'class="[^"]*archive[^"]*"') {
        $dest = $blogFolder # Assuming archives are blog articles
    }
    
    # Copy the HTML file
    Copy-Item -Path $file.FullName -Destination $dest -Force
    
    # Extract images: look for wp-content/uploads/...
    $regex = 'wp-content/uploads/([a-zA-Z0-9_/\.\-]+(?:jpg|jpeg|png|gif|webp|svg))'
    $matches = [regex]::Matches($content, $regex, 'IgnoreCase')
    
    foreach ($m in $matches) {
        $relPath = $m.Groups[1].Value
        $localImgPath = Join-Path $oldFolder "wp-content\uploads\$relPath"
        
        # Some URLs might have %20 or other encoded chars, maybe unescape?
        $localImgPath = [System.Uri]::UnescapeDataString($localImgPath)
        # Fix slashes
        $localImgPath = $localImgPath -replace '/', '\'
        
        if (Test-Path $localImgPath) {
            # Try to keep the file name
            $imgName = Split-Path $localImgPath -Leaf
            $imgDest = Join-Path $dest $imgName
            if (!(Test-Path $imgDest)) {
                Copy-Item -Path $localImgPath -Destination $imgDest -Force
            }
        }
    }
}
Write-Host 'Done.'
