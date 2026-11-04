# Deployment helper script for Vercel Relay
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "   Trien khai MotoMap Zalo Webhook Relay (Vercel)  " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "1. Dang chay 'npx vercel --prod'..." -ForegroundColor Yellow
Write-Host "Luu y: Neu trinh duyet bat len, hay dang nhap bang GitHub/Email (Mien phi 100%)." -ForegroundColor Gray
Write-Host ""

Set-Location $PSScriptRoot
npx vercel --prod --yes

Write-Host ""
Write-Host "==================================================" -ForegroundColor Green
Write-Host "Trien khai hoan tat! Copy link tren va gan vao Zalo Bot." -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
